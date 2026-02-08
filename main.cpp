// 02-05: Update recent profit per coin (could have to use nodes bc A->B->C->A != B -> C -> A -> B idk)

#include "Common.hpp"
#include "SymbolMap.hpp"
#include "GraphManager.hpp"
#include "ArbitrageEngine.hpp"
#include "RingBuffer.hpp"
#include "BinanceConnector.hpp"
#include "DataRecorder.hpp"

#include <iostream>
#include <thread>
#include <memory>
#include <atomic>
#include <chrono>
#include <boost/asio.hpp>
#include <csignal>
#include <iomanip>

#include <immintrin.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>

namespace net = boost::asio;
constexpr int BATCH_SIZE = 1000;
constexpr int BIT_SHIFT = 7;
constexpr int MAX_BUCKET = 1000000;
static long long latency_histogram[MAX_BUCKET];
static long long total_ns = 0;
static long long count = 0;
static long long outliers = 0;

std::atomic<bool> g_running{true};

void printStats(int signum) {
    g_running = false;
    std::cout << "\n\n >>> 10-Hour Live Benchmark Results <<<" << std::endl;

    if (count == 0) { std::cout << "No Data. " << std::endl; exit(0);}

    double avg_ns = (double)total_ns / count;

    long long sum = 0;
    long long p50_idx = 0, p99_idx = 0, p999_idx = 0;
    long long target_50 = count * 0.50;
    long long target_99 = count * 0.99;
    long long target_999 = count * 0.999;

    for (int i = 0; i < 1000000; i++) {
        sum += latency_histogram[i];
        if (p50_idx == 0 && sum >= target_50) p50_idx = i;
        if (p99_idx == 0 && sum >= target_99) p99_idx = i;
        if (p999_idx == 0 && sum >= target_999) {
            p999_idx = i;
            break;
        }
    }

    std::cout << "Total Updates : " << count << std::endl;
    std::cout << "Average Latency: " << std::fixed << std::setprecision(3) << avg_ns / 1000.0 << " us" << std::endl;
    std::cout << "p50 (Median)   : " << p50_idx / 10.0 << " us" << std::endl; // 100ns 단위라 /10 하면 us
    std::cout << "p99            : " << p99_idx / 10.0 << " us" << std::endl;
    std::cout << "p99.9          : " << p999_idx / 10.0 << " us" << std::endl;
    std::cout << "Outliers (>100ms): " << outliers << std::endl;

}

void PinThreadToCore(int core_id, const std::string& thread_name) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);

    if (rc != 0) {
        std::cerr << "[Warning] Failed to pin " << thread_name << " to Core " << core_id << std::endl;
    } else {
        std::cout << "[System] " << thread_name << " is PINNED to Core " << core_id << std::endl;
    }
}

void SetRealtimePriority() {
    struct sched_param param;
    
    param.sched_priority = 90;

    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        std::cerr << "Somehow failed scheduler set idk. " << std::endl;
    } else {
        std::cerr << "yippee" << std::endl;
    }
}

void NetworkThread(std::shared_ptr<net::io_context> ioc) {
    PinThreadToCore(2, "Network Thread");

    std::cout << "[Network] Thread Started. Connecting to Binance..." << std::endl;
    auto work = net::make_work_guard(*ioc);
    ioc->run();
    std::cout << "[Network] Stopped." << std::endl;
}
int main() {
    std::signal(SIGINT, printStats);
    long howmanySPFA = 0;
    try {
        std::cout << "Initializing..." << "\n";
        PinThreadToCore(4, "Engine(Main)");
        SetRealtimePriority();
        // 1. 초기화
        SymbolMap sm;
        sm.Init();

        GraphManager gm;
        gm.Init();

        ArbitrageEngine engine;

        auto ring_buffer = std::make_shared<RingBuffer<TickerUpdate>>(65536);

        std::shared_ptr<net::io_context> ioc = std::make_shared<net::io_context>();

        ssl::context ctx{ssl::context::tlsv12_client};
        ctx.set_verify_mode(ssl::verify_none);

        auto connector = std::make_shared<BinanceConnector>(*ioc, ctx, ring_buffer, sm);

        connector->run("stream.binance.us", "9443", "/ws");

        std::thread net_thread(NetworkThread, ioc);

        std::cout << ">>> Engine Started. Waiting for Market Data..." << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::cout << ">>> Elevating Priority to SCHED_FIFO..." << std::endl;
        SetRealtimePriority();

        TickerUpdate update;
        NodeID last_detected_node = 0;
        auto start_time = std::chrono::steady_clock::now();
        // DataRecorder recorder("market_data.bin");

        while (g_running) {

            auto batch_start = std::chrono::steady_clock::now();

            int processed_in_batch = 0;

            bool has_new_data = false;
            static bool dirty_nodes[128]; 
            std::memset(dirty_nodes, 0, sizeof(dirty_nodes));

            while (processed_in_batch < BATCH_SIZE && ring_buffer->dequeue(update)) {
                // recorder.Write(update);
                gm.UpdateWeight(update.edge_idx, update.price);
                dirty_nodes[update.u] = true;
                has_new_data = true;
                processed_in_batch++;
            }
            if (!has_new_data) {
                _mm_pause();
                continue;
            }

            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < Config::NUM_COINS; i++) {
                if (dirty_nodes[i]) {
                    engine.DetectCycle(gm, sm, now, i);
                    howmanySPFA++;
                }
            }

            // engine.ProcessCheck(gm);

            auto batch_end = std::chrono::steady_clock::now();
            long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(batch_end - batch_start).count();

            if (processed_in_batch > 0) {
                long ns_per_op = ns / processed_in_batch;

                total_ns += ns;
                count += processed_in_batch;

                int bucket = ns_per_op >> 7; // /128
                if (bucket < 1000000) {
                    latency_histogram[bucket] += processed_in_batch;
                } else {
                    outliers += processed_in_batch;
                }
            }
        }

        
        auto end_time = std::chrono::steady_clock::now();
        std::cout << std::endl;
        std::chrono::duration<double> diff = end_time - start_time;
        double seconds = diff.count();
        // engine.PrintLogs();
        ioc->stop();
        net_thread.join();
        std::cout << "\n\n>>> Shutdown Signal Received." << std::endl;
        std::cout << "==========================================" << std::endl;
        std::cout << " Total Updates Processed : " << count << std::endl;
        std::cout << " Running Time            : " << seconds << " sec" << std::endl;
        std::cout << " Throughput              : " << (count / seconds) << " ops/sec" << std::endl;
        std::cout << " SPFAs                   : " << howmanySPFA << std::endl;
        std::cout << "==========================================" << std::endl;
        engine.PrintMinMaxLatency();

        

    } catch (std::exception& e) {
        std::cerr << "[Critical Error] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
