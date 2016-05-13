/*
Copyright (c) 2016 Erik Rigtorp <erik@rigtorp.se>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */

#pragma once

#include <atomic>
#include <cassert>
#include <limits>
#include <stdexcept>

namespace rigtorp {

template <typename T> class MPMCQueue {
private:
  static_assert(std::is_nothrow_copy_assignable<T>::value &&
                    std::is_nothrow_move_assignable<T>::value,
                "T must be nothrow copy or move assignable");

  static_assert(std::is_nothrow_destructible<T>::value,
                "T must be nothrow destructible");

public:
  explicit MPMCQueue(const size_t capacity)
      : capacity_(capacity), slots_(new Slot[capacity + 2 * kPadding]),
        head_(0), tail_(0) {
    if (capacity_ < 1) {
      throw std::invalid_argument("capacity < 1");
    }
    if (!slots_) {
      throw std::bad_alloc();
    }

    assert(alignof(MPMCQueue<T>) >= kCacheLineSize);
    assert(reinterpret_cast<char *>(&tail_) -
               reinterpret_cast<char *>(&head_) >=
           kCacheLineSize);
  }

  ~MPMCQueue() { delete[] slots_; }

  // non-copyable and non-movable
  MPMCQueue(const MPMCQueue &) = delete;
  MPMCQueue &operator=(const MPMCQueue &) = delete;

  template <typename... Args> void emplace(Args &&... args) {
    static_assert(std::is_nothrow_constructible<T, Args...>::value,
                  "T must be nothrow constructible");
    auto const head = head_.fetch_add(1);
    auto &slot = slots_[idx(head)];
    while (turn(head) * 2 != slot.turn.load(std::memory_order_acquire))
      ;
    slot.construct(std::forward<Args>(args)...);
    slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
  }

  template <typename... Args> bool try_emplace(Args &&... args) {
    static_assert(std::is_nothrow_constructible<T, Args...>::value,
                  "T must be nothrow constructible");
    auto head = head_.load(std::memory_order_acquire);
    for (;;) {
      auto &slot = slots_[idx(head)];
      if (turn(head) * 2 == slot.turn.load(std::memory_order_acquire)) {
        if (head_.compare_exchange_strong(head, head + 1)) {
          slot.construct(std::forward<Args>(args)...);
          slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
          return true;
        }
      } else {
        auto const prevHead = head;
        head = head_.load(std::memory_order_acquire);
        if (head == prevHead) {
          return false;
        }
      }
    }
  }

  void push(T &&v) { emplace(std::forward<T>(v)); }

  bool try_push(T &&v) { return try_emplace(std::forward<T>(v)); }

  void pop(T &v) {
    auto const tail = tail_.fetch_add(1);
    auto &slot = slots_[idx(tail)];
    while (turn(tail) * 2 + 1 != slot.turn.load(std::memory_order_acquire))
      ;
    v = slot.move();
    slot.destroy();
    slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
  }

  bool try_pop(T &v) {
    auto tail = tail_.load(std::memory_order_acquire);
    for (;;) {
      auto &slot = slots_[idx(tail)];
      if (turn(tail) * 2 + 1 == slot.turn.load(std::memory_order_acquire)) {
        if (tail_.compare_exchange_strong(tail, tail + 1)) {
          v = slot.move();
          slot.destroy();
          slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
          return true;
        }
      } else {
        auto const prevTail = tail;
        tail = tail_.load(std::memory_order_acquire);
        if (tail == prevTail) {
          return false;
        }
      }
    }
  }

private:
  constexpr size_t idx(size_t i) const noexcept {
    return i % capacity_ + kPadding;
  }
  constexpr size_t turn(size_t i) const noexcept { return i / capacity_; }

  static constexpr size_t kCacheLineSize = 128;

  struct Slot {
    ~Slot() noexcept {
      if (turn & 1) {
        destroy();
      }
    }

    template <typename... Args> void construct(Args &&... args) noexcept {
      new (&storage) T(std::forward<Args>(args)...);
    }
    void destroy() noexcept { reinterpret_cast<T *>(&storage)->~T(); }
    T &&move() noexcept { return reinterpret_cast<T &&>(storage); }

    // Align to avoid false sharing between adjacent slots
    alignas(kCacheLineSize) std::atomic<size_t> turn = {0};
    std::aligned_storage<sizeof(T), alignof(T)> storage;
  };

  // Padding to avoid false sharing between slots_ and adjacent allocations
  static constexpr size_t kPadding = (kCacheLineSize - 1) / sizeof(Slot) + 1;

private:
  const size_t capacity_;
  Slot *const slots_;

  // Align to avoid false sharing between head_ and tail_
  alignas(kCacheLineSize) std::atomic<size_t> head_;
  alignas(kCacheLineSize) std::atomic<size_t> tail_;

  // Padding to avoid adjacent allocations to share cache line with tail_
  char padding_[kCacheLineSize - sizeof(tail_)];
};
}
