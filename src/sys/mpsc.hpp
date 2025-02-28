//
// Created by Loulfy on 25/09/2021.
//

#pragma once

#include <atomic>
#include <new>

namespace ler::sys
{
#ifdef __cpp_lib_hardware_interference_size
using std::hardware_constructive_interference_size;
using std::hardware_destructive_interference_size;
#else
// 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │ ...
constexpr std::size_t hardware_constructive_interference_size = 64;
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

template <typename T> class MpscQueue
{
  public:
    MpscQueue();
    MpscQueue(const MpscQueue&) = delete;
    MpscQueue(MpscQueue&&) = delete;
    ~MpscQueue();

    MpscQueue& operator=(const MpscQueue&) = delete;
    MpscQueue& operator=(MpscQueue&&) = delete;

    bool enqueue(const T& item);
    bool enqueue(T&& item);
    bool dequeue(T& item);

  private:
    struct Node
    {
        std::atomic<Node*> next;
        T value;
    };

    alignas(hardware_destructive_interference_size) std::atomic<Node*> _head;
    alignas(hardware_destructive_interference_size) std::atomic<Node*> _tail;
};

#include "mpsc.inl"
} // namespace ler::sys