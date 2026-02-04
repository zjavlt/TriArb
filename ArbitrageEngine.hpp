#pragma once
#include "Common.hpp"
#include "GraphManager.hpp"
#include "OrderExecutor.hpp"
#include <vector>
#include <cstring>
#include <chrono>
constexpr int64_t MIN_IMPROVEMENT = 100000; //ln(1.0001) * 1e9 = 99995

template <typename T, int SIZE>
class StaticQueue {
    T buffer[SIZE];
    int head = 0;
    int tail = 0;
    int count = 0;
    static_assert((SIZE & (SIZE - 1)) == 0, "Queue SIZE must be power of 2");
    static constexpr int MASK = SIZE - 1;

public:
    void clear() {head = 0; tail = 0; count = 0;}
    bool empty() {return count == 0;}

    void push(T val) {
        buffer[tail] = val;
        tail = (tail + 1) & MASK;
        count++;
    }

    T pop() {
        if (count == 0) return T{};
        T val = buffer[head];
        head = (head + 1) & MASK;
        count--;
        return val;
    }
};

class ArbitrageEngine {
private:
    //buffer for SPFA
    int64_t dist[MAX_NODES];
    NodeID parent[MAX_NODES];
    int update_cnt[MAX_NODES]; // cycle detection counter
    bool in_queue[MAX_NODES];
    StaticQueue<NodeID, MAX_NODES * 16> q; // 2^n for bitmask
    long total_detection = 0;
    OrderExecutor executor;
    std::chrono::steady_clock::time_point last_detection_time;
    NodeID last_detected_node = -1;

public:
    long max_latency = 0;
    long min_latency = 9999;
    
    ArbitrageEngine() {
        Reset();
    }

    void Reset() {
        std::memset(update_cnt, 0, sizeof(update_cnt));
        std::memset(in_queue, 0, sizeof(in_queue));

        // std::memset(dist, GraphManager::INF_WEIGHT, sizeof(dist));
        q.clear();
    }

    long GetTotalDetection() {
        return total_detection;
    }

    void PrintMinMaxLatency() {
        std::cout << "[Maximum Latency] " << max_latency << "us\n[Minimum Latency] " << min_latency 
        << "us\n[Total Detections] " << total_detection << std::endl;
    }

    // SPFA
    // update_edge_idx -> later optimize to update only edges connected to the most recently updated node
    // full scan for now
    void DetectCycle(GraphManager& gm, const SymbolMap& sm, std::chrono::steady_clock::time_point recv_time) {
        Reset();
        for (int i = 0; i < MAX_NODES; ++i) {
            dist[i] = GraphManager::INF_WEIGHT;
        }
        for (int i = 0; i < Config::NUM_COINS; i++) {
            q.push(i);
            in_queue[i] = true;
            dist[i] = 0;
        }
        long loop_count = 0;
        long relax_count = 0;

        while (!q.empty()) {
            NodeID u = q.pop();
            in_queue[u] = false;
            loop_count++; // [Debug] 루프 횟수 체크

            // 여기서 gm.adj_size[u]가 0이면 그래프 연결이 안 된 것임
            int count = gm.adj_size[u];
            if (count == 0 && loop_count < 20) {
                 // [Debug] 연결된 간선이 없음 (초반에만 출력)
                 std::cout << "[Debug] Node " << u << " has 0 edges!" << std::endl; 
            }

            for (int i = 0; i < count; ++i){
                const auto& edge = gm.adj[u][i];

                NodeID v = edge.to;
                int64_t weight = gm.edge_weights[edge.weight_idx];

                if (weight >= GraphManager::INF_WEIGHT) continue;

                if (dist[u] + weight + MIN_IMPROVEMENT < dist[v]) {
                    dist[v] = dist[u] + weight;
                    parent[v] = u;
                    update_cnt[v]++;
                    relax_count++;

                    // if (relax_count < 10) std::cout << "[Debug] Relax: " << u << "->" << v << " W:" << weight << std::endl;

                    if (update_cnt[v] > Config::NUM_COINS) {
                        auto now = std::chrono::steady_clock::now();
                        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(now - recv_time).count();
                        if (latency > max_latency) max_latency = latency;
                        if (latency < min_latency) min_latency = latency;
                        total_detection++;

                        // auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_detection_time).count();
                        // // std::cout << "[Perf] Cycle Detected! Internal Latency: " << latency << " us" << std::endl;
                        // // if (v != last_detected_node || time_diff > 100) {
                        // //     // ProcessArbitrage(v, gm, sm);
                        // //     last_detection_time = now;
                        // //     last_detected_node = v;
                        // // }
                        return;
                    }

                    if (!in_queue[v]) {
                        q.push(v);
                        in_queue[v] = true;
                    }
                }
            }
        }
        // std::cout << "[Debug] SPFA Finished. Loops: " << loop_count << ", Relaxations: " << relax_count << std::endl;
    }

private:
    void ProcessArbitrage(NodeID detected_node, GraphManager& gm, const SymbolMap& sm) {
        NodeID curr = detected_node;
        for (int i = 0 ; i < Config::NUM_COINS; i++) {
            curr = parent[curr];
        }

        std::vector<NodeID> cycle;
        NodeID start = curr;

        while (true) {
            cycle.push_back(curr);
            if (curr == start && cycle.size() > 1) break;
            curr = parent[curr];
            
            // 안전장치: 무한루프 방지
            if (cycle.size() > Config::NUM_COINS * 2) return;
        }

        //backtracked so backward
        executor.Execute(cycle, gm, sm);
    }
};