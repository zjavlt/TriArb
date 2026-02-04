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
        std::cout << "[SymbolMap] Initializing sequence for " << Config::NUM_COINS << "coins... " << std::endl;

        symbol_to_node.reserve(Config::NUM_COINS);   
        pair_to_edge.reserve(Config::NUM_COINS * (Config::NUM_COINS - 1));
        binance_to_edges.reserve(Config::NUM_COINS * (Config::NUM_COINS - 1));

        for (int i = 0; i < Config::NUM_COINS; i++) {
            symbol_to_node.emplace(std::string(Config::COINS[i]), static_cast<NodeID>(i));
        }

        int id = 0;
        std::string pair_buffer;
        pair_buffer.reserve(16); // 문자열 메모리 할당 방지

        for (int i = 0 ; i < Config::NUM_COINS; i++) {
            for (int j = 0; j < Config::NUM_COINS; j++) {
                if (i==j) continue;
                    pair_buffer = Config::COINS[i];
                    pair_buffer += Config::COINS[j];
                    pair_to_edge[pair_buffer] = id;
                    id++;
            }
        }

        // 4. [핵심] Binance Symbol 매핑 (여기가 비어있으면 BTCUSDT도 못 찾음)
        int mapped_count = 0;
        for (int i = 0; i < Config::NUM_COINS; i++) {
            for (int j = 0; j < Config::NUM_COINS; j++) {
                if (i == j) continue;
                
                // i=BTC, j=USDT -> "BTCUSDT"
                std::string symbol = std::string(Config::COINS[i]) + std::string(Config::COINS[j]);
                
                // 만약 pair_to_edge에 없으면 로직 에러
                if (pair_to_edge.find(symbol) == pair_to_edge.end()) continue;

                EdgeID fwd = pair_to_edge[symbol];
                
                // 역방향 (USDT + BTC)
                std::string rev_symbol = std::string(Config::COINS[j]) + std::string(Config::COINS[i]);
                if (pair_to_edge.find(rev_symbol) == pair_to_edge.end()) continue;
                
                EdgeID bwd = pair_to_edge[rev_symbol];

                // 맵에 저장
                binance_to_edges[symbol] = {fwd, bwd};
                mapped_count++;
            }
        }
        std::cout << "[SymbolMap] Successfully mapped " << mapped_count << " trading pairs." << std::endl;
        
        // [Self-Check] BTCUSDT가 잘 들어갔는지 확인
        if (binance_to_edges.find("BTCUSDT") != binance_to_edges.end()) {
            std::cout << "[Check] BTCUSDT is VALID." << std::endl;
        } else {
            std::cerr << "[Check] BTCUSDT is MISSING!" << std::endl;
        }
        if (binance_to_edges.find("BTCUSD") != binance_to_edges.end()) {
            std::cout << "[Check] BTCUSD is VALID." << std::endl;
        } else {
            std::cerr << "[Check] BTCUSD is MISSING! (Did you add 'USD' to Config::COINS?)" << std::endl;
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


