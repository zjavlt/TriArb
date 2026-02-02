#pragma once
#include "Common.hpp"
#include "SymbolMap.hpp"
#include <vector>
#include <cmath>
#include <iostream>

class GraphManager {
public:
    std::vector<Edge> adj[MAX_NODES];

    std::vector<int64_t> edge_weights;

    const double FEE_MULTIPLIER = 1.0 - 0.00075;

    static constexpr int64_t SCALING_FACTOR = 1000000000;

    void Init() {
        int max_possible_edges = NUM_COINS * (NUM_COINS - 1);
        edge_weights.resize(max_possible_edges, 0);

        int edge_cnt = 0;

        for (int u = 0; u < NUM_COINS; u++) {
            for (int v = 0; v < NUM_COINS; v++) {
                if (u == v) continue;

                Edge e;
                e.to = v;
                e.weight_idx = edge_cnt;
                
                adj[u].push_back(e);
                edge_cnt++;
            }
        }
        std::cout << "[GraphManager] Initialized with " << edge_cnt << " edges." << std::endl;
    }

    inline void UpdateWeight(EdgeID id, double price) {
        if (id < 0 || id >= (int)edge_weights.size()) return;

        if (price > 1e-9) {
            double log_val = -std::log(price * FEE_MULTIPLIER);
            edge_weights[id] = static_cast<int64_t>(log_val * SCALING_FACTOR);
        }
    }

    double GetOriginalPrice(EdgeID id) {
        double log_val = (double)edge_weights[id] / SCALING_FACTOR;
        return std::exp(-log_val) / FEE_MULTIPLIER;
    }
};