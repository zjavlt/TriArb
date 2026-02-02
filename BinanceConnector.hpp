#pragma once
#include "MarketDataConnector.hpp"
#include <simdjson.h>
#include <string>

class BinanceConnector : public MarketDataConnector {
private:
    simdjson::ondemand::parser parser_;
    simdjson::padded_string json_data_;

public:
    BinanceConnector(net::io_context& ioc, ssl::context& ctx, std::shared_ptr<RingBuffer<TickerUpdate>> queue, const SymbolMap& sm) 
        : MarketDataConnector(ioc, ctx, queue, sm) {}

protected:
    void on_session_started() override {
        // Top 20 코인에 대한 Book Ticker 구독
        // 하나씩 구독하면 메시지 제한 걸릴 수 있으므로, 
        // 실전에서는 Combined Stream을 쓰거나 리스트를 묶어서 보냄.
        // 여기서는 예시로 "모든 심볼(!bookTicker)"을 구독하거나 
        // SymbolMap에 있는 것들만 루프 돌며 구독 요청.
        
        // 예: Combined Stream 사용 (소문자 주의)
        // wss://stream.binance.com:9443/ws/btcusdt@bookTicker/ethusdt@bookTicker/...
        
        // 일단 간단하게 단일 구독 메시지 전송 예시:
        std::string sub_msg = R"({"method": "SUBSCRIBE", "params": ["btcusdt@bookTicker", "ethusdt@bookTicker", "ethbtc@bookTicker"], "id": 1})";
        
        // *실제 구현 시*: SymbolMap의 COIN_LIST를 조합해 필요한 Pair만 구독 문자열 생성 필요.
        ws_.write(net::buffer(sub_msg));
    }
    void process_message(std::string_view data) override {
        json_data_ = simdjson::padded_string(data);
        try {
            auto doc = parser_.iterate(json_data_);

            std::string_view s_sv;
            if (doc["s"].get_string().get(s_sv) != simdjson::SUCCESS) return;
            // 1. Symbol Parsing -> Edge ID Lookup
            // "BTCUSDT"가 오면 -> 우리는 "BTC"+"USDT" 와 "USDT"+"BTC" 두 개를 찾아야 함.
            // SymbolMap이 "BTCUSDT"를 "BTC->USDT"로 매핑한다고 가정.
            // 역방향 문자열을 만드는 비용이 있으므로, 
            // SymbolMap에 "BTCUSDT" -> {EdgeID_Forward, EdgeID_Backward} 둘 다 저장해두는 게 베스트.
            
            // [최적화 Tip] SymbolMap에 GetPairIDs("BTCUSDT") 함수를 만들어 pair<int, int>를 리턴하게 하면 좋음.
            // 여기서는 기존 GetEdgeID 활용:
            // "BTCUSDT" -> Edge(BTC -> USDT) : BTC를 Bid에 매도
            int edge_fwd = symbol_map_.GetEdgeID(std::string(s_sv)); 
            
            // 역방향 ID 찾기 (문자열 조작 비용 발생... 개선 필요)
            // 일단 로직 흐름만 보여드림. 실제론 Init 때 Reverse Map도 만들어야 함.
            // 지금은 Fwd만 처리하거나, SymbolMap 업그레이드 필요.
            // --- 데이터 파싱 (std::stod 대신 simdjson의 double 파싱 사용) ---
            std::string_view bid_str, ask_str;
            doc["b"].get_string().get(bid_str); // Bid Price
            doc["a"].get_string().get(ask_str); // Ask Price
            
            // Simdjson은 문자열 안에 있는 숫자를 바로 double로 변환하는 기능 제공 (unsafe but fast)
            // 혹은 fast_float 라이브러리 사용. 여기서는 std::stod로 임시 처리 (병목 지점 1)
            double bid_px = std::stod(std::string(bid_str));
            double ask_px = std::stod(std::string(ask_str));

            // Time
            long ts = 0; // Ticker엔 보통 event time이 없음, 수신 시간 사용하거나 u값 사용
            
            // --- Queue Push ---
            
            // 1. Forward Edge (BTC -> USDT): Sell at Bid
            if (edge_fwd != -1) {
                TickerUpdate t;
                t.edge_idx = edge_fwd;
                t.price = bid_px; // 이 가격에 팜
                t.timestamp = ts;
                queue_->enqueue(t);
            }

            // 2. Backward Edge (USDT -> BTC): Buy at Ask
            // "USDTBTC" 문자열을 만들어서 ID 찾는 건 너무 느림.
            // SymbolMap 구조를 바꿔서, "BTCUSDT" 입력 시 {fwd_id, bwd_id}를 한번에 줘야 함.
            // (아래에서 SymbolMap 수정 제안)
        }catch (simdjson::simdjson_error&) {}
    }
};