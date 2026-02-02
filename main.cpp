#include "Common.hpp"
#include "GraphManager.hpp"
#include <iostream>

int main() {
    SymbolMap sm;
    sm.Init();

    GraphManager gm;
    gm.Init();

    // Test: BTCUSDT 가격 업데이트
    int edge_id = sm.GetEdgeID("BTCUSDT");
    std::cout << "BTCUSDT Edge ID: " << edge_id << std::endl;

    gm.UpdateWeight(edge_id, 95000.0);
    std::cout << "Weight stored: " << gm.edge_weights[edge_id] << std::endl;
    std::cout << "Weight initially: " << gm.GetOriginalPrice(edge_id) << std::endl;

    return 0;
}