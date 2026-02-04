#pragma once
#include "Common.hpp"
#include "SymbolMap.hpp"
#include <vector>
#include <cmath>
#include <iostream>
#include <cstring>

class GraphManager {
public:
    int64_t edge_weights[MAX_EDGES];
    struct SimpleEdge {
        NodeID to;
        EdgeID weight_idx;
    };
    SimpleEdge adj[MAX_NODES][MAX_NODES]; 
    int adj_size[MAX_NODES];

    const double FEE_MULTIPLIER = 1.0 - 0.00075;
    static constexpr int64_t INF_WEIGHT = 1000000000000000;
    static constexpr int64_t LOG_FEE_SCALED = 0; //-999500; for testing//0 for no fee; //750281; // -log(1 - 0.00075) * 1e9
    static constexpr int64_t SCALING_FACTOR = 1000000000;

    void Init() {
        std::fill(std::begin(edge_weights), std::end(edge_weights), INF_WEIGHT);
        std::memset(adj_size, 0, sizeof(adj_size));

        int edge_cnt = 0;
        for (int u = 0; u < NUM_COINS; u++) {
            for (int v = 0; v < NUM_COINS; v++) {
                if (u == v) continue;
                int idx = adj_size[u]++;
                adj[u][idx] = {(NodeID)v, edge_cnt};
                edge_cnt++;
            }
        }
        std::cout << "[GraphManager] Initialized with " << edge_cnt << " edges." << std::endl;
    }

    inline void UpdateWeight(EdgeID id, double price) {
        if (__builtin_expect(price > 1e-9, 1)) {
            double log_p = -std::log(price);
            edge_weights[id] = static_cast<int64_t>(log_p * SCALING_FACTOR) + LOG_FEE_SCALED;
        }
    }

    double GetOriginalPrice(EdgeID id) {
        int64_t val = edge_weights[id] - LOG_FEE_SCALED;
        double log_p = (double)val / SCALING_FACTOR; // = -log(price)
        return std::exp(-log_p);
    }
};