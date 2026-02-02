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

class SymbolMap {
public:
    std::unordered_map<std::string, NodeID> symbol_to_node;
    std::unordered_map<std::string, EdgeID> pair_to_edge;
    void Init() {

        symbol_to_node.reserve(NUM_COINS);   
        pair_to_edge.reserve(NUM_COINS * (NUM_COINS - 1));
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
    }

    inline EdgeID GetEdgeID(const std::string& symbol) {
        auto it = pair_to_edge.find(symbol);
        if (it != pair_to_edge.end()) {
            return it->second;
        }
        return -1; // 리스트에 없는 코인 쌍(예: TRXBTC)이 들어올 경우 무시
    }

    // 디버깅용: EdgeID -> "BTC/USDT"
    std::string GetPairName(NodeID from, NodeID to) const {
        return std::string(COIN_LIST[from]) + "/" + std::string(COIN_LIST[to]);
    }
};


