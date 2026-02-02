#pragma once
#include "Common.hpp"
#include "GraphManager.hpp"
#include "OrderExecutor.hpp"
#include <vector>
#include <cstring>

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
        tail = (tail + 1) & MASK; // bit manipulation later?
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

    StaticQueue<NodeID, MAX_NODES * 4> q; // 4 for bitmask

    OrderExecutor executor;

public:
    ArbitrageEngine() {
        Reset();
    }

    void Reset() {
        std::memset(update_cnt, 0, sizeof(update_cnt));
        std::memset(in_queue, 0, sizeof(in_queue));

        // std::memset(dist, GraphManager::INF_WEIGHT, sizeof(dist));
        q.clear();
    }

    // SPFA
    // update_edge_idx -> later optimize to update only edges connected to the most recently updated node
    // full scan for now
    void DetectCycle(GraphManager& gm, const SymbolMap& sm) {
        Reset();
        for (int i = 0; i < MAX_NODES; ++i) {
            dist[i] = GraphManager::INF_WEIGHT;
        }
        for (int i = 0; i < NUM_COINS; i++) {
            q.push(i);
            in_queue[i] = true;
        }

        while (!q.empty()) {
            NodeID u = q.pop();
            in_queue[u] = false;

            for (const auto& edge : gm.adj[u]) {
                NodeID v = edge.to;
                int64_t weight = gm.edge_weights[edge.weight_idx];

                if (weight >= GraphManager::INF_WEIGHT) continue;

                if (dist[u] + weight < dist[v]) {
                    dist[v] = dist[u] + weight;
                    parent[v] = u;
                    update_cnt[v]++;

                    if (update_cnt[v] > NUM_COINS) {
                        ProcessArbitrage(v, gm, sm);
                        return;
                    }

                    if (!in_queue[v]) {
                        q.push(v);
                        in_queue[v] = true;
                    }
                }
            }
        }
    }

private:
    void ProcessArbitrage(NodeID detected_node, GraphManager& gm, const SymbolMap& sm) {
        NodeID curr = detected_node;
        for (int i = 0 ; i < NUM_COINS; i++) {
            curr = parent[curr];
        }

        std::vector<NodeID> cycle;
        NodeID start = curr;

        while (true) {
            cycle.push_back(curr);
            if (curr == start && cycle.size() > 1) break;
            curr = parent[curr];
            
            // 안전장치: 무한루프 방지
            if (cycle.size() > NUM_COINS * 2) return;
        }

        //backtracked so backward
        executor.Execute(cycle, gm, sm);
    }
};