/*
Copyright (c) 2017 Erik Rigtorp <erik@rigtorp.se>

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
#include <memory>
#include <stdexcept>

namespace rigtorp {

template <typename T> class MPMCQueue {
private:
  static_assert(std::is_nothrow_copy_assignable<T>::value ||
                    std::is_nothrow_move_assignable<T>::value,
                "T must be nothrow copy or move assignable");

  static_assert(std::is_nothrow_destructible<T>::value,
                "T must be nothrow destructible");

public:
  explicit MPMCQueue(const size_t capacity)
      : capacity_(capacity), head_(0), tail_(0) {
    if (capacity_ < 1) {
      throw std::invalid_argument("capacity < 1");
    }
    size_t space = capacity * sizeof(Slot) + kCacheLineSize - 1;
    buf_ = malloc(space);
    if (buf_ == nullptr) {
      throw std::bad_alloc();
    }
    void *buf = buf_;
    slots_ = reinterpret_cast<Slot *>(
        std::align(kCacheLineSize, capacity * sizeof(Slot), buf, space));
    if (slots_ == nullptr) {
      free(buf_);
      throw std::bad_alloc();
    }
    for (size_t i = 0; i < capacity_; ++i) {
      new (&slots_[i]) Slot();
    }
    static_assert(sizeof(MPMCQueue<T>) % kCacheLineSize == 0,
                  "MPMCQueue<T> size must be a multiple of cache line size to "
                  "prevent false sharing between adjacent queues");
    static_assert(sizeof(Slot) % kCacheLineSize == 0,
                  "Slot size must be a multiple of cache line size to prevent "
                  "false sharing between adjacent slots");
    assert(reinterpret_cast<size_t>(slots_) % kCacheLineSize == 0 &&
           "slots_ array must be aligned to cache line size to prevent false "
           "sharing between adjacent slots");
    assert(reinterpret_cast<char *>(&tail_) -
                   reinterpret_cast<char *>(&head_) >=
               kCacheLineSize &&
           "head and tail must be a cache line apart to prevent false sharing");
  }

  ~MPMCQueue() noexcept {
    for (size_t i = 0; i < capacity_; ++i) {
      slots_[i].~Slot();
    }
    free(buf_);
  }

  // non-copyable and non-movable
  MPMCQueue(const MPMCQueue &) = delete;
  MPMCQueue &operator=(const MPMCQueue &) = delete;

  template <typename... Args> void emplace(Args &&... args) noexcept {
    static_assert(std::is_nothrow_constructible<T, Args &&...>::value,
                  "T must be nothrow constructible with Args&&...");
    auto const head = head_.fetch_add(1);
    auto &slot = slots_[idx(head)];
    while (turn(head) * 2 != slot.turn.load(std::memory_order_acquire))
      ;
    slot.construct(std::forward<Args>(args)...);
    slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
  }

  template <typename... Args> bool try_emplace(Args &&... args) noexcept {
    static_assert(std::is_nothrow_constructible<T, Args &&...>::value,
                  "T must be nothrow constructible with Args&&...");
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

  void push(const T &v) noexcept {
    static_assert(std::is_nothrow_copy_constructible<T>::value,
                  "T must be nothrow copy constructible");
    emplace(v);
  }

  template <typename P,
            typename = typename std::enable_if<
                std::is_nothrow_constructible<T, P &&>::value>::type>
  void push(P &&v) noexcept {
    emplace(std::forward<P>(v));
  }

  bool try_push(const T &v) noexcept {
    static_assert(std::is_nothrow_copy_constructible<T>::value,
                  "T must be nothrow copy constructible");
    return try_emplace(v);
  }

  template <typename P,
            typename = typename std::enable_if<
                std::is_nothrow_constructible<T, P &&>::value>::type>
  bool try_push(P &&v) noexcept {
    return try_emplace(std::forward<P>(v));
  }

  void pop(T &v) noexcept {
    auto const tail = tail_.fetch_add(1);
    auto &slot = slots_[idx(tail)];
    while (turn(tail) * 2 + 1 != slot.turn.load(std::memory_order_acquire))
      ;
    v = slot.move();
    slot.destroy();
    slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
  }

  bool try_pop(T &v) noexcept {
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
  constexpr size_t idx(size_t i) const noexcept { return i % capacity_; }

  constexpr size_t turn(size_t i) const noexcept { return i / capacity_; }

  static constexpr size_t kCacheLineSize = 128;

  struct Slot {
    ~Slot() noexcept {
      if (turn & 1) {
        destroy();
      }
    }

    template <typename... Args> void construct(Args &&... args) noexcept {
      static_assert(std::is_nothrow_constructible<T, Args &&...>::value,
                    "T must be nothrow constructible with Args&&...");
      new (&storage) T(std::forward<Args>(args)...);
    }

    void destroy() noexcept {
      static_assert(std::is_nothrow_destructible<T>::value,
                    "T must be nothrow destructible");
      reinterpret_cast<T *>(&storage)->~T();
    }

    T &&move() noexcept { return reinterpret_cast<T &&>(storage); }

    // Align to avoid false sharing between adjacent slots
    alignas(kCacheLineSize) std::atomic<size_t> turn = {0};
    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
  };

private:
  const size_t capacity_;
  Slot *slots_;
  void *buf_;

  // Align to avoid false sharing between head_ and tail_
  alignas(kCacheLineSize) std::atomic<size_t> head_;
  alignas(kCacheLineSize) std::atomic<size_t> tail_;
};
}
