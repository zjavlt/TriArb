#pragma once
#include "Common.hpp"
#include "GraphManager.hpp"
#include "SymbolMap.hpp"
#include <vector>
#include <iostream>
#include <algorithm>
#include <iomanip>

class OrderExecutor {
public:
    void Execute(std::vector<NodeID>& reverse_path, GraphManager& gm, const SymbolMap& sm) {
        std::vector<NodeID> path = reverse_path;
        std::reverse(path.begin(), path.end());

        if (path.front() != path.back()) return;

        std::cout << "\n[!] ARBITRAGE FOUND! Path: ";
        
        double total_log_return = 0.0;

        for (size_t i = 0; i < path.size() - 1; i++) {
            NodeID u = path[i];
            NodeID v = path[i+1];

            double original_price = 0.0;

            for (auto& e : gm.adj[u]) {
                if (e.to == v) {
                    original_price = gm.GetOriginalPrice(e.weight_idx);
                    total_log_return += (double)gm.edge_weights[e.weight_idx] / GraphManager::SCALING_FACTOR;
                    break;
                }
            }

            std::cout << sm.GetPairName(u, v) << "(" << original_price << ") -> ";
        }
        std::cout << "LOOP" << "\n";

        double actual_return_pct = (std::exp(-total_log_return) - 1.0) * 100.0;

        std::cout << ">>> Expected Profit: " << std::fixed << std::setprecision(9)
                  << actual_return_pct << "% (Net)" << std::endl;
    }
};