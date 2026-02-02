// 02-01: Finished SPFA and RingBuffer, need optimization on digraph edges -> then start parsing

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
#include <immintrin.h> //need clarification later CPU level commands

namespace net = boost::asio;

// [Global] 프로그램 실행 상태 플래그
// 시그널 핸들러에서 접근해야 하므로 전역(혹은 정적)이어야 함
std::atomic<bool> g_running{true};

// [Handler] Ctrl+C가 눌리면 호출됨
void signal_handler(int signum) {
    g_running = false;
}

void NetworkThread(std::shared_ptr<net::io_context> ioc) {
    std::cout << "[Network] Thread Started. Connecting to Binance..." << std::endl;
    auto work = net::make_work_guard(*ioc);
    ioc->run();
    std::cout << "[Network] Stopped." << std::endl;
}
int main() {
    try {
        std::signal(SIGINT, signal_handler);
        // 1. 초기화
        SymbolMap sm;
        sm.Init();

        GraphManager gm;
        gm.Init();

        ArbitrageEngine engine;

        auto ring_buffer = std::make_shared<RingBuffer<TickerUpdate>>(4096);

        std::shared_ptr<net::io_context> ioc = std::make_shared<net::io_context>();

        ssl::context ctx{ssl::context::tlsv12_client};
        ctx.set_verify_mode(ssl::verify_none);

        auto connector = std::make_shared<BinanceConnector>(*ioc, ctx, ring_buffer, sm);

        connector->run("stream.binance.us", "9443", "/ws");

        std::thread injector([&gm, &sm, ring_buffer](){
            std::this_thread::sleep_for(std::chrono::seconds(5));
            std::cout << "\n>>> [Test] Injecting FAKE for Latency Check! <<<\n" << std::endl;
            
            EdgePair edges = sm.GetEdgePair("DOGEUSDT");
            if (edges.fwd != -1) {
                TickerUpdate fake;
                fake.edge_idx = edges.fwd;
                fake.price = 100.0;
                // 여기서 시간을 찍어서 보냄
                fake.recv_time = std::chrono::steady_clock::now(); 
                ring_buffer->enqueue(fake);
            }
        });
        injector.detach();

        std::thread net_thread(NetworkThread, ioc);

        std::cout << ">>> Engine Started. Waiting for Market Data..." << std::endl;

        TickerUpdate update;
        long processed_count = 0;
        auto start_time = std::chrono::steady_clock::now();

        while (g_running) {
            if (ring_buffer->dequeue(update)) {
                
                // [디버깅] 초반 5개 데이터는 무조건 출력 (데이터 들어오는지 확인용)
                if (processed_count < 5) {
                    std::cout << "[Debug] Data received! Edge: " << update.edge_idx 
                              << " Price: " << update.price << std::endl;
                }

                gm.UpdateWeight(update.edge_idx, update.price);
                engine.DetectCycle(gm, sm, update.recv_time);

                processed_count++;
                
                // 10만 건은 너무 멂. 1,000건마다 점(.)을 찍어서 생존 신고
                if (processed_count % 10000 == 0) {
                     std::cout << "." << std::flush;
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

        // Cleanup
        ioc->stop();
        net_thread.join();

    } catch (std::exception& e) {
        std::cerr << "[Critical Error] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
