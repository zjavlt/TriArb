#include "SymbolMap.hpp"
#include "GraphManager.hpp"
#include "ArbitrageEngine.hpp"
#include <iostream>

// 02-01: Finished SPFA and RingBuffer, need optimization on digraph edges -> then start parsing

int main() {
    // 1. 초기화
    SymbolMap sm;
    sm.Init();

    GraphManager gm;
    gm.Init();

    ArbitrageEngine engine;

    std::cout << "[System] Engine Initialized. Injecting Mock Data..." << std::endl;

    // 2. 정상적인 시장 상황 (차익 기회 없음)
    // A -> B -> C -> A 곱해서 1.0 근처가 되도록 설정
    // USDT -> BTC ($95000)
    // BTC -> ETH (33.33 ETH/BTC ... 가정)
    // ETH -> USDT ($2850)
    // 95000 * (1/2850) * (1/33.33) ~= 1.0
    
    int e1 = sm.GetEdgeID("BTCUSDT"); // USDT -> BTC (매수: 1/Price) 주의! 
    // 아비트라지 방향성을 명확히 해야 함. 일단 단순하게 Price로 넣고 엔진 테스트.
    // 여기서는 그래프의 가중치 방향이 중요함.
    // GraphManager는 -log(price)를 저장함.
    
    // 시나리오: USDT ->(사고)-> BTC ->(사고)-> ETH ->(팔고)-> USDT
    // 1. USDT로 BTC 매수: Price = 1/95000 (역수)
    // 2. BTC로 ETH 매수: Price = 1/0.03 (역수)
    // 3. ETH를 USDT로 매도: Price = 3000
    // Profit = (1/95000) * (1/0.03) * 3000 
    //        = 3000 / 2850 = 1.05 (5% 이득!)

    // Edge ID 조회 (SymbolMap.hpp의 로직에 따라 문자열 결합)
    // 주의: SymbolMap은 "From"+"To"로 저장함.
    // USDT->BTC 엣지는 "USDTBTC" 임. 하지만 바이낸스 심볼은 "BTCUSDT"
    // HFT에서는 보통 "BTCUSDT" 가격을 받으면 "USDT->BTC" 와 "BTC->USDT" 두 엣지를 갱신함.
    // 하나는 Price, 하나는 1/Price 로.
    // 일단 테스트를 위해 직접 ID를 가져와서 조작.
    
    NodeID usdt = sm.symbol_to_node["USDT"];
    NodeID btc = sm.symbol_to_node["BTC"];
    NodeID eth = sm.symbol_to_node["ETH"];

    EdgeID usdt_btc = sm.GetEdgeID("USDTBTC"); // USDT -> BTC
    EdgeID btc_eth  = sm.GetEdgeID("BTCETH");  // BTC -> ETH
    EdgeID eth_usdt = sm.GetEdgeID("ETHUSDT"); // ETH -> USDT

    // 3. Arbitrage 기회 주입 (5% 이득 시나리오)
    // 수수료 고려해서 넉넉하게 잡음
    gm.UpdateWeight(usdt_btc, 1.0 / 10000.0); // 1 BTC = 10,000 USDT
    gm.UpdateWeight(btc_eth,  1.0 / 0.05);    // 1 ETH = 0.05 BTC (20 ETH/BTC)
    gm.UpdateWeight(eth_usdt, 600.0);         // 1 ETH = 600 USDT
    
    // 계산: 10,000으로 나눠서 BTC 사고(0.0001), 20배 해서 ETH 되고(0.002), 600 곱하면(1.2)
    // 1.0 -> 1.2 (20% 수익)

    // 4. 엔진 실행
    engine.DetectCycle(gm, sm);

    return 0;
}