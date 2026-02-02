#pragma once
#include "Common.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <array>

constexpr std::array<std::string_view, NUM_COINS> COIN_LIST = {
    "USDT", "BTC", "ETH", "BNB", "XRP", "SOL", 
    "DOGE", "ADA", "LTC", "BCH", "LINK"
};

struct EdgePair {
    EdgeID fwd; // A- > B
    EdgeID bwd; // B -> A
};

class SymbolMap {
public:
    std::unordered_map<std::string_view, NodeID> symbol_to_node;
    std::unordered_map<std::string_view, EdgeID> pair_to_edge;
    std::unordered_map<std::string_view, EdgePair> binance_to_edges;
    void Init() {

        symbol_to_node.reserve(NUM_COINS);   
        pair_to_edge.reserve(NUM_COINS * (NUM_COINS - 1));
        binance_to_edges.reserve(NUM_COINS * (NUM_COINS - 1));
        for (int i = 0; i < NUM_COINS; i++) {
            symbol_to_node.emplace(std::string(COIN_LIST[i]), static_cast<NodeID>(i));
        }

        int id = 0;
        std::string pair_buffer;
        pair_buffer.reserve(16); // 문자열 메모리 할당 방지

        for (int i = 0 ; i < NUM_COINS; i++) {
            for (int j = 0; j < NUM_COINS; j++) {
                if (i==j) continue;
                    pair_buffer = COIN_LIST[i];
                    pair_buffer += COIN_LIST[j];
                    pair_to_edge[pair_buffer] = id;
                    id++;
            }
        }

        for (int i = 0; i < NUM_COINS; i++) {
            for (int j = 0; j < NUM_COINS; j++) {
                if (i==j)continue;
                std::string symbol = std::string(COIN_LIST[i]) + std::string(COIN_LIST[j]);

                EdgeID fwd = GetEdgeID(symbol);
                EdgeID bwd = GetEdgeID(std::string(COIN_LIST[j]) + std::string(COIN_LIST[i]));

                binance_to_edges[symbol] = {fwd, bwd};
            }
        }
    }

    inline EdgeID GetEdgeID(const std::string_view& symbol) {
        auto it = pair_to_edge.find(symbol);
        if (it != pair_to_edge.end()) {
            return it->second;
        }
        return -1; // 리스트에 없는 코인 쌍(예: TRXBTC)이 들어올 경우 무시
    }

    inline EdgePair GetEdgePair(const std::string_view& symbol) const {
        auto it = binance_to_edges.find(symbol); // string view로 바꿨으니까 그에 맞게 업데이트; 현재 string 기준임
        if (it != binance_to_edges.end()) return it -> second;
        return {-1, -1};
    }

    // 디버깅용: EdgeID -> "BTC/USDT"
    std::string GetPairName(NodeID from, NodeID to) const {
        return std::string(COIN_LIST[from]) + "/" + std::string(COIN_LIST[to]);
    }
};


