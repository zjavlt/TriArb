#pragma once
#include "Common.hpp"
#include <atomic>
#include <vector>
#include <cstddef>
#include <thread>
#include <iostream>


constexpr size_t CACHE_LINE_SIZE = 64;

template<typename T>
class RingBuffer {
private:
    //each buffer slot
    struct alignas(64) Element {
        T data;

        std::atomic<size_t> sequence;
    };

    //member var
    const size_t buffer_mask_;
    Element* buffer_;

    //alignas: allocate these vars in independant cache lines
    //also to prevent false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_; // producer location
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_; // consumer location

public:
    //size must be 2**n
    RingBuffer(size_t size) 
        : buffer_mask_(size - 1), buffer_(new Element[size]), tail_(0), head_(0)
    {
        // slot reset
        for (size_t i = 0; i < size; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~RingBuffer() { delete[] buffer_;}

    //enqueue
    bool enqueue(T data) {
        size_t cell_sequence;
        size_t pos = tail_.load(std::memory_order_relaxed);

        while (true) {
            // slot check
            Element& cell = buffer_[pos & buffer_mask_]; // faster modulo with bit

            cell_sequence = cell.sequence.load(std::memory_order_acquire);

            intptr_t dif = (intptr_t)cell_sequence - (intptr_t)pos;

            if (dif == 0) {

                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    //slot successfully taken
                    cell.data = data;

                    //update sequence number
                    // using release ensures data is updated first
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            }
            //room sequence is smaller than my sequence -> consumer did not read and update (occupied)
            else if (dif < 0) {
                return false;
            }
            // dif > 0: other producer already passed by -> load tail again
            else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    bool dequeue(T& data) {
        size_t cell_sequence;
        size_t pos = head_.load(std::memory_order_relaxed);

        while (true) {
            Element& cell = buffer_[pos & buffer_mask_];
            cell_sequence = cell.sequence.load(std::memory_order_acquire); // acquire receives release
            
            //check if sequence is pos + 1 after producer used data
            intptr_t dif = (intptr_t)cell_sequence - (intptr_t)(pos + 1);

            if (dif == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    data = cell.data;

                    cell.sequence.store(pos + buffer_mask_ + 1, std::memory_order_release);
                    return true;
                }
            }
            // data not ready (producer did not use yet)
            else if (dif < 0) {
                return false; //queue empty
            }
            // head already changed (ABA). update position to the new head
            else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }
};


