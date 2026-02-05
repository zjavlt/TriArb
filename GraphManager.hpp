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
    static constexpr int64_t LOG_FEE_SCALED = 82503; 
    //-999500; for testing//0 for no fee; //750281 -> -log(1 - 0.00075) * 1e9 //82503 -> -log(1 - 0.0000825) 
    static constexpr int64_t SCALING_FACTOR = 1000000000;

    //gm
    void Init() {
        std::fill(std::begin(edge_weights), std::end(edge_weights), INF_WEIGHT);
        std::memset(adj_size, 0, sizeof(adj_size));

        for (const auto& edge : Config::VALID_EDGES) {
             // 엣지 ID 계산 (u*N + v 등 규칙에 따름)
             int edge_id = edge.u * MAX_NODES + edge.v; 
             
             adj[edge.u][adj_size[edge.u]] = { (NodeID)edge.v, edge_id };
             adj_size[edge.u]++;
        }
    }

    double GetWeight(NodeID u, NodeID v) const {
        for (int i = 0; i < adj_size[u]; i++) {
            if (adj[u][i].to == v) {
                return edge_weights[adj[u][i].weight_idx];
            }
        }

        return INF_WEIGHT;
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