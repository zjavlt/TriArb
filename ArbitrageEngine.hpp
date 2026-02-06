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

struct PendingCheck {
    std::chrono::steady_clock::time_point detect_time;
    double expected_profit;
    NodeID path[10];
    int path_len;
    bool active;
};

struct TradeLog {
    long latency_us;
    double exp_profit;
    double act_profit;
    NodeID path[10];
    int path_len;
    bool success;
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
    static constexpr long STALE_THRESHOLD_US = 500;
    
    //sim
    std::vector<PendingCheck> pending_queue; //later change to array or custom queue for optimization
    int pending_idx = 0;
    const long SIM_LATENCY = 50000; //microseconds

    std::chrono::steady_clock::time_point last_schedule_time;
    double last_scheduled_profit = 0.0;

    //log
    static constexpr int MAX_LOGS = 16384; //adjustable
    TradeLog log_buffer[MAX_LOGS];
    int log_head = 0;
    int log_count = 0;

public:
    long max_latency = 0;
    long min_latency = 9999;
    long skipped = 0;
    
    ArbitrageEngine() {
        Reset();
        q.clear();
        pending_queue.resize(128);
        for (auto& p: pending_queue) p.active = false;
    }

    void Reset() {
        std::memset(update_cnt, 0, sizeof(update_cnt));
        std::memset(in_queue, 0, sizeof(in_queue));

        // std::memset(dist, GraphManager::INF_WEIGHT, sizeof(dist));
    }

    void ScheduleCheck(NodeID detected_node, const GraphManager& gm) {
        // std::cout << "Schedule check " << std::endl;
        auto& item = pending_queue[pending_idx];

        item.detect_time = std::chrono::steady_clock::now();
        item.active = true;

        NodeID curr = detected_node;
        for (int i = 0; i < Config::NUM_COINS; i++) {
            curr = parent[curr];
        }

        NodeID start = curr;
        int idx = 0;

        while (true) {
            if (idx >= 10) break; //modify able

            item.path[idx] = curr;
            idx++;

            curr = parent[curr];
            if (curr == start && idx > 1) {
                item.path[idx++] = curr;
                break;
            }
        }

        item.path_len = idx;

        std::reverse(item.path, item.path + item.path_len);

        double real_log_sum = 0.0;
        bool path_valid = true;

        for (int i = 0; i < item.path_len - 1; i++) {
            NodeID u = item.path[i];
            NodeID v = item.path[i+1];
            
            // GM에서 현재 가중치 조회
            int64_t w = gm.GetWeight(u, v); 
            if (w >= GraphManager::INF_WEIGHT) {
                path_valid = false; 
                break;
            }
            real_log_sum += w;
        }
        if (path_valid) {
            // 여기서 단위 변환 (1e9)
            // Sum은 음수여야 이득 (Log space)
            double real_sum_dbl = (double)real_log_sum / 1000000000.0; 
            item.expected_profit = std::exp(-real_sum_dbl) - 1.0; // 진짜 예상 수익
        } else {
            item.expected_profit = 0.0; // 경로 깨짐
        }

        // [Deduplication] 중복 제거 로직 (여기서 수행)
        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_schedule_time).count();

        // 500ms 내에 같은 수익률이면 스킵
        if (diff < 500 && std::abs(item.expected_profit - last_scheduled_profit) < 0.00001) {
            item.active = false; // 큐에 넣은 척만 하고 비활성
            return; 
        }

        // 갱신
        last_schedule_time = now;
        last_scheduled_profit = item.expected_profit;

        pending_idx = (pending_idx + 1) & 127;
    }

    void ProcessCheck(const GraphManager& gm) {
        // std::cout << "Process check" << std::endl;
        auto now = std::chrono::steady_clock::now();

        for (auto& item : pending_queue) {
            if (!item.active) continue;
            auto diff = std::chrono::duration_cast<std::chrono::microseconds>(now - item.detect_time).count();

            if (diff >= SIM_LATENCY) {
                double current_log_sum = 0.0;
                bool valid = true;

                for (int i = 0;i < item.path_len - 1; i++) {
                    NodeID u = item.path[i];
                    NodeID v = item.path[i+1];

                    int64_t weight = gm.GetWeight(u, v);

                    if (weight >= GraphManager::INF_WEIGHT) {
                        valid = false;
                        break;
                    }
                    current_log_sum += weight;
                }

                if (valid) {
                    double current_profit = std::exp(-((double)current_log_sum / GraphManager::SCALING_FACTOR)) - 1.0;

                    TradeLog& log = log_buffer[log_head];
                    log.latency_us = diff;
                    log.exp_profit = item.expected_profit;
                    log.act_profit = current_profit;
                    log.path_len = item.path_len;
                    std::memcpy(log.path, item.path, sizeof(NodeID) * item.path_len);
                    log.success = (current_profit > 0);

                    log_head = (log_head + 1) & (MAX_LOGS - 1);
                    if (log_count < MAX_LOGS) log_count++;
                }

                item.active = false;
            }
        }
    }

    void PrintLogs() {
        std::cout << "\n>>> Trade History Dump (" << log_count << " entries) <<<" << std::endl;

        int start_idx = (log_count < MAX_LOGS) ? 0 : log_head;

        for (int i = 0; i < log_count; i++) {
            int idx = (start_idx + i) & (MAX_LOGS - 1);
            const TradeLog& log = log_buffer[idx];

            std::cout << "[Cycle] ";
            for (int k = 0; k < log.path_len; k++) {
                std::cout << Config::COINS[log.path[k]];
                if (k < log.path_len - 1) std::cout << " -> ";
            }

            std::cout << " | Exp: " << std::fixed << std::setprecision(5) << log.exp_profit * 100  << "%"
                      << " -> Act: " << log.act_profit * 100 << "% ";
            if (log.success) std::cout << "[WIN]";
            else std::cout << "[Decayed]";

            std::cout << " (" << log.latency_us << "us later)" << std::endl;
        }
    }

    long GetTotalDetection() {
        return total_detection;
    }

    void PrintMinMaxLatency() {
        std::cout << "[Maximum Latency] " << max_latency << "us\n[Minimum Latency] " << min_latency 
        << "us\n[Total Detections] " << total_detection 
        << "\n[Skipped] " << skipped << std::endl;
    }

    void DetectCycle(GraphManager& gm, const SymbolMap& sm, std::chrono::steady_clock::time_point recv_time, NodeID source_node) {
        auto now = std::chrono::steady_clock::now();
        auto late = std::chrono::duration_cast<std::chrono::microseconds>(now - recv_time).count();
        if (late > STALE_THRESHOLD_US) {
            skipped++;
            return;
        }
        Reset();
        for (int i = 0; i < MAX_NODES; ++i) {
            dist[i] = GraphManager::INF_WEIGHT;
        }
        
        dist[source_node]= 0;
        q.push(source_node);
        in_queue[source_node] = true;
        
        long loop_count = 0;
        long relax_count = 0;

        while (!q.empty()) {
            NodeID u = q.pop();
            in_queue[u] = false;
            loop_count++;

            int count = gm.adj_size[u];
            if (count == 0 && loop_count < 20) {
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
                        // std::cout << "detection test" << std::endl;
                        auto now = std::chrono::steady_clock::now();
                        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(now - recv_time).count();
                        if (latency > max_latency) max_latency = latency;
                        if (latency < min_latency) min_latency = latency;
                        total_detection++;

                        if (dist[v] < -0.00001) {
                            ScheduleCheck(v, gm);
                            return;
                        }
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