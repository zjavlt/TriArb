#pragma once
#include <iostream>
#include <chrono>

using NodeID = int;
using EdgeID = int;
constexpr int MAX_NODES = 16; // 2^k for bitmask
constexpr int MAX_EDGES = MAX_NODES * MAX_NODES;

struct TickerUpdate {
    EdgeID edge_idx;
    double price; //int_64t later?
    std::chrono::steady_clock::time_point recv_time;
};

struct Edge {
    NodeID to; // target node
    EdgeID weight_idx; // weight array index
};