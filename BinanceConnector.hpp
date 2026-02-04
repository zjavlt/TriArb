#pragma once
#include "MarketDataConnector.hpp"
#include <simdjson.h>
#include <string>
#include "SymbolMap.hpp"
#include <sstream>
#include <cctype>
#include "Config.hpp"

class BinanceConnector : public MarketDataConnector {
private:
    simdjson::ondemand::parser parser_;
    simdjson::padded_string json_data_;

public:
    BinanceConnector(net::io_context& ioc, ssl::context& ctx, std::shared_ptr<RingBuffer<TickerUpdate>> queue, const SymbolMap& sm) 
        : MarketDataConnector(ioc, ctx, queue, sm) {}

protected:
    void on_session_started() override {

        std::stringstream ss;
        ss << R"({"method": "SUBSCRIBE", "params": [)";
        
        for (size_t i = 0; i < Config::TARGET_PAIRS.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "\"" << Config::TARGET_PAIRS[i] << "@bookTicker\"";
        }
        
        ss << R"(], "id": 1})";
        
        ws_.async_write(net::buffer(ss.str()), 
            [this](beast::error_code ec, std::size_t bytes_transferred) {
                if (ec) std::cerr << "Subscribe Failed: " << ec.message() << std::endl;
                else std::cout << "[Binance] Subscribed to " << Config::NUM_PAIRS << " valid pairs." << std::endl;
            });
    }

    void process_message(std::string_view data) override {
        // std::cout << data << std::endl;

        // simdjson은 padding이 필요하므로 string_view를 padded_string으로 변환 (복사 비용 1회 발생)
        // 최적화하려면 수신 버퍼 자체를 padded로 관리해야 하지만, 지금은 이정도로 충분함.
        json_data_ = simdjson::padded_string(data);

        try {
            auto doc = parser_.iterate(json_data_);

            std::string_view s_sv;
            if (doc["s"].get_string().get(s_sv) != simdjson::SUCCESS) return;
            
            EdgePair edges = symbol_map_.GetEdgePair(std::string(s_sv));

            if (edges.fwd == -1)  {
                return;
            }
            std::string_view bid_str, ask_str;
            double bid_price = 0.0, ask_price = 0.0;

            if (doc["b"].get_string().get(bid_str) == simdjson::SUCCESS) {
                // std::stod보다 fast_float 라이브러리가 빠르지만, 표준 의존성 위해 stod 사용
                // 문자열이 null-terminated가 아닐 수 있으므로 string 변환 후 stod
                bid_price = std::stod(std::string(bid_str));
            }

            if (doc["a"].get_string().get(ask_str) == simdjson::SUCCESS) {
                ask_price = std::stod(std::string(ask_str));
            }

            // 타임스탬프 (Optional, 여기선 로컬 시간 혹은 패킷 내 시간 사용)
            long ts = 0; // doc["T"].get_int64() ...
            
            // --- Queue Push ---
            auto now = std::chrono::steady_clock::now();
            // Case A: Forward Edge (예: BTC -> USDT)
            
            if (bid_price > 0) {
                TickerUpdate t;
                t.edge_idx = edges.fwd;
                t.price = bid_price; // 이 가격에 팜
                t.recv_time = now;
                queue_->enqueue(t);
            }

            if (ask_price > 0) {
                TickerUpdate t;
                t.edge_idx = edges.bwd;
                t.price = 1.0 / ask_price;
                t.recv_time = now;

                queue_->enqueue(t);
            }
        }catch (simdjson::simdjson_error&) {}
        catch (std::exception& e) {
            std::cerr << "Parser Error: " << e.what() << std::endl;
        }
    }
};