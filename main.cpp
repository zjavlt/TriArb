// 02-04: Process SPFA by batches (100)
// Current maximum latency: 79us

#include "Common.hpp"
#include "SymbolMap.hpp"
#include "GraphManager.hpp"
#include "ArbitrageEngine.hpp"
#include "RingBuffer.hpp"
#include "BinanceConnector.hpp"

#include <iostream>
#include <thread>
#include <memory>
#include <atomic>
#include <chrono>
#include <boost/asio.hpp>
#include <csignal>

#include <immintrin.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>

namespace net = boost::asio;
constexpr int BATCH_SIZE = 1000;

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    g_running = false;
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
    std::signal(SIGINT, signal_handler);
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
        long processed_count = 0;
        auto start_time = std::chrono::steady_clock::now();

        while (g_running) {
            int processed_in_batch = 0;

            std::chrono::steady_clock::time_point last_recv_time;
            bool has_new_data = false;

            while (processed_in_batch < BATCH_SIZE && ring_buffer->dequeue(update)) {
                gm.UpdateWeight(update.edge_idx, update.price);
                last_recv_time = update.recv_time;
                has_new_data = true;
                processed_in_batch++;

            }
            if (has_new_data) {
                engine.DetectCycle(gm, sm, last_recv_time);

                processed_count += processed_in_batch;
            } else {
                _mm_pause();
            }
        }

        

        // -------------------------------------------------------
        // [Exit Stats] 종료 시 통계 출력
        // -------------------------------------------------------
        auto end_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> diff = end_time - start_time;
        double seconds = diff.count();

        std::cout << "\n\n>>> Shutdown Signal Received." << std::endl;
        std::cout << "==========================================" << std::endl;
        std::cout << " Total Updates Processed : " << processed_count << std::endl;
        std::cout << " Running Time            : " << seconds << " sec" << std::endl;
        std::cout << " Throughput              : " << (processed_count / seconds) << " ops/sec" << std::endl;
        std::cout << "==========================================" << std::endl;
        engine.PrintMinMaxLatency();

        // Cleanup
        ioc->stop();
        net_thread.join();

    } catch (std::exception& e) {
        std::cerr << "[Critical Error] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
