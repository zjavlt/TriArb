// 02-03: Got it to work with locality and core focus and FIFO scheduler. Now going to expand coin graph with python scraping. 
// Current average delay : 40 microseconds

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

// [Global] 프로그램 실행 상태 플래그
// 시그널 핸들러에서 접근해야 하므로 전역(혹은 정적)이어야 함
std::atomic<bool> g_running{true};

// [Handler] Ctrl+C가 눌리면 호출됨
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

        // std::thread injector([&gm, &sm, ring_buffer](){
        //     PinThreadToCore(6, "Injector"); // diff core
        //     std::this_thread::sleep_for(std::chrono::seconds(5));
        //     std::cout << "\n>>> [Test] Injecting FAKE for Latency Check! <<<\n" << std::endl;
            
        //     EdgePair edges = sm.GetEdgePair("DOGEUSDT");
        //     if (edges.fwd != -1) {
        //         TickerUpdate fake;
        //         fake.edge_idx = edges.fwd;
        //         fake.price = 100.0;
        //         // 여기서 시간을 찍어서 보냄
        //         fake.recv_time = std::chrono::steady_clock::now(); 
        //         ring_buffer->enqueue(fake);
        //     }
        // });
        // injector.detach();

        std::thread net_thread(NetworkThread, ioc);

        std::cout << ">>> Engine Started. Waiting for Market Data..." << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::cout << ">>> Elevating Priority to SCHED_FIFO..." << std::endl;
        SetRealtimePriority();

        TickerUpdate update;
        long processed_count = 0;
        auto start_time = std::chrono::steady_clock::now();

        while (g_running) {
            if (ring_buffer->dequeue(update)) {

                gm.UpdateWeight(update.edge_idx, update.price);
                engine.DetectCycle(gm, sm, update.recv_time);

                processed_count++;
                
                // 10만 건은 너무 멂. 1,000건마다 점(.)을 찍어서 생존 신고
                if (processed_count % 100000 == 0) {
                    long q_size = ring_buffer->size();
                    
                    // [Diagnosis]
                    // q_size가 0 또는 1에 수렴해야 정상 (엔진이 네트워크보다 빠름)
                    // q_size가 계속 100, 1000 단위로 늘어나면 -> 엔진이 느림 (병목)
                    
                    std::cout << "[Stats] Latency Max: " << engine.max_latency << " us"
                            << " | Queue Size: " << q_size 
                            << " | Arbitrage found: " << engine.GetTotalDetection()
                            << std::endl;
                    
                }
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
