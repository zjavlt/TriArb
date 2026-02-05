#pragma once
#include "Common.hpp"
#include "Config.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <array>
#include <iostream>

struct EdgePair {
    EdgeID fwd; // A- > B
    EdgeID bwd; // B -> A
};

class SymbolMap {
public:
    std::unordered_map<std::string, NodeID> symbol_to_node;
    std::unordered_map<std::string, EdgeID> pair_to_edge;
    std::unordered_map<std::string, EdgePair> binance_to_edges;
    void Init() {
        std::cout << "Initializing symbol map..." <<"\n";
        
        symbol_to_node.reserve(Config::NUM_COINS);
        for (int i = 0; i < Config::NUM_COINS; i++) {
            symbol_to_node.emplace(std::string(Config::COINS[i]), static_cast<NodeID>(i));
        }

        binance_to_edges.reserve(Config::VALID_EDGES.size());
        int mapped_count = 0;

        for (const auto& edge : Config::VALID_EDGES) {
            std::string symbol = std::string(Config::COINS[edge.u]) + std::string(Config::COINS[edge.v]);
            EdgeID fwd_id = edge.u * MAX_NODES + edge.v;
            EdgeID bwd_id = edge.v * MAX_NODES + edge.u;

            binance_to_edges[symbol] = {fwd_id, bwd_id};
            mapped_count++;
        }
        std::cout << "[SymbolMap] Successfully mapped " << mapped_count << " trading pairs." << std::endl;
        
        // [Verification]
        if (binance_to_edges.find("BTCUSDT") != binance_to_edges.end()) {
            std::cout << "[Check] BTCUSDT is VALID." << std::endl;
        } else {
            std::cerr << "[Check] BTCUSDT is MISSING!" << std::endl;
        }
    }

    inline EdgePair GetEdgePair(const std::string_view symbol) const {
        auto it = binance_to_edges.find(std::string(symbol)); // string view로 바꿨으니까 그에 맞게 업데이트; 현재 string 기준임
        if (it != binance_to_edges.end()) return it -> second;
        return {-1, -1};
    }

    // 디버깅용: EdgeID -> "BTC/USDT"
    std::string GetPairName(NodeID from, NodeID to) const {
        return std::string(Config::COINS[from]) + "/" + std::string(Config::COINS[to]);
    }
};


