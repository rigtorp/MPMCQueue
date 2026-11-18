/*
Copyright (c) 2020 Erik Rigtorp <erik@rigtorp.se>

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
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace rigtorp {

template <typename T, typename Allocator = std::allocator<T>>
  requires std::is_nothrow_destructible_v<T>
class MPMCQueue {
  // Match SPSCQueue's spacing, including 128-byte pairs used by x86 spatial
  // prefetchers.
#if defined(__s390x__)
  static constexpr std::size_t kFalseSharingRange = 256;
#elif defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) ||        \
    defined(_M_ARM64) || defined(__powerpc64__)
  static constexpr std::size_t kFalseSharingRange = 128;
#else
  static constexpr std::size_t kFalseSharingRange = 64;
#endif

  struct Slot {
    Slot() noexcept : turn(0) {}

    ~Slot() noexcept {
      if (turn & 1) {
        destroy();
      }
    }

    template <typename... Args> void construct(Args &&...args) noexcept {
      std::construct_at(reinterpret_cast<T *>(storage),
                        std::forward<Args>(args)...);
    }

    void destroy() noexcept { std::destroy_at(value()); }

    T &&move() noexcept { return std::move(*value()); }

    // Align to avoid false sharing between adjacent slots.
    alignas(kFalseSharingRange) std::atomic<std::size_t> turn;

  private:
    T *value() noexcept { return std::launder(reinterpret_cast<T *>(storage)); }

    alignas(T) std::byte storage[sizeof(T)];
  };

  using SlotAllocator =
      typename std::allocator_traits<Allocator>::template rebind_alloc<Slot>;
  using SlotAllocatorTraits = std::allocator_traits<SlotAllocator>;

  static_assert(std::is_same_v<typename SlotAllocatorTraits::pointer, Slot *>,
                "The rebound allocator must use raw pointers");

public:
  explicit MPMCQueue(const std::size_t capacity,
                     const Allocator &allocator = Allocator())
      : capacity_(capacity), allocator_(allocator), head_(0), tail_(0) {
    if (capacity_ == 0) [[unlikely]] {
      throw std::invalid_argument("capacity < 1");
    }
    if (capacity_ == std::numeric_limits<std::size_t>::max()) [[unlikely]] {
      throw std::bad_array_new_length();
    }
    // Allocate one extra slot to prevent false sharing on the last slot
    slots_ = SlotAllocatorTraits::allocate(allocator_, capacity_ + 1);
    // Allocators are not required to honor alignment for over-aligned types
    // (see http://eel.is/c++draft/allocator.requirements#10) so we verify
    // alignment here
    if (reinterpret_cast<std::uintptr_t>(slots_) % alignof(Slot) != 0)
        [[unlikely]] {
      deallocate_slots();
      throw std::bad_alloc();
    }
    for (std::size_t i = 0; i < capacity_; ++i) {
      std::construct_at(&slots_[i]);
    }
    static_assert(
        alignof(Slot) >= kFalseSharingRange,
        "Slot must be aligned to cache line boundary to prevent false sharing");
    static_assert(sizeof(Slot) % kFalseSharingRange == 0,
                  "Slot size must be a multiple of cache line size to prevent "
                  "false sharing between adjacent slots");
    static_assert(sizeof(MPMCQueue) % kFalseSharingRange == 0,
                  "Queue size must be a multiple of cache line size to "
                  "prevent false sharing between adjacent queues");
  }

  ~MPMCQueue() noexcept {
    for (std::size_t i = 0; i < capacity_; ++i) {
      std::destroy_at(&slots_[i]);
    }
    deallocate_slots();
  }

  // non-copyable and non-movable
  MPMCQueue(const MPMCQueue &) = delete;
  MPMCQueue &operator=(const MPMCQueue &) = delete;

  template <typename... Args>
    requires std::is_nothrow_constructible_v<T, Args &&...>
  void emplace(Args &&...args) noexcept {
    auto const head = head_.fetch_add(1);
    auto &slot = slots_[idx(head)];
    auto const t = turn(head);
    while (t * 2 != slot.turn.load(std::memory_order_acquire))
      ;
    slot.construct(std::forward<Args>(args)...);
    slot.turn.store(t * 2 + 1, std::memory_order_release);
  }

  template <typename... Args>
    requires std::is_nothrow_constructible_v<T, Args &&...>
  [[nodiscard]] bool try_emplace(Args &&...args) noexcept {
    auto head = head_.load(std::memory_order_acquire);
    for (;;) {
      auto &slot = slots_[idx(head)];
      auto const t = turn(head);
      if (t * 2 == slot.turn.load(std::memory_order_acquire)) {
        if (head_.compare_exchange_strong(head, head + 1)) {
          slot.construct(std::forward<Args>(args)...);
          slot.turn.store(t * 2 + 1, std::memory_order_release);
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

  void push(const T &v) noexcept
    requires std::is_nothrow_copy_constructible_v<T>
  {
    emplace(v);
  }

  template <typename P>
    requires std::is_nothrow_constructible_v<T, P &&>
  void push(P &&v) noexcept {
    emplace(std::forward<P>(v));
  }

  [[nodiscard]] bool try_push(const T &v) noexcept
    requires std::is_nothrow_copy_constructible_v<T>
  {
    return try_emplace(v);
  }

  template <typename P>
    requires std::is_nothrow_constructible_v<T, P &&>
  [[nodiscard]] bool try_push(P &&v) noexcept {
    return try_emplace(std::forward<P>(v));
  }

  void pop(T &v) noexcept
    requires(std::is_nothrow_move_assignable_v<T> ||
             std::is_nothrow_copy_assignable_v<T>)
  {
    auto const tail = tail_.fetch_add(1);
    auto &slot = slots_[idx(tail)];
    auto const t = turn(tail);
    while (t * 2 + 1 != slot.turn.load(std::memory_order_acquire))
      ;
    assign(v, slot.move());
    slot.destroy();
    slot.turn.store(t * 2 + 2, std::memory_order_release);
  }

  [[nodiscard]] bool try_pop(T &v) noexcept
    requires(std::is_nothrow_move_assignable_v<T> ||
             std::is_nothrow_copy_assignable_v<T>)
  {
    return try_consume([&v](T &&item) noexcept { assign(v, std::move(item)); });
  }

  template <typename Consumer>
    requires requires(Consumer &&c, T &&item) {
      { std::forward<Consumer>(c)(std::move(item)) } noexcept;
    }
  [[nodiscard]] bool try_consume(Consumer &&c) noexcept {
    auto tail = tail_.load(std::memory_order_acquire);
    for (;;) {
      auto &slot = slots_[idx(tail)];
      auto const t = turn(tail);
      if (t * 2 + 1 == slot.turn.load(std::memory_order_acquire)) {
        if (tail_.compare_exchange_strong(tail, tail + 1)) {
          std::forward<Consumer>(c)(slot.move());
          slot.destroy();
          slot.turn.store(t * 2 + 2, std::memory_order_release);
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

  /// Returns the number of elements in the queue.
  /// The size can be negative when the queue is empty and there is at least one
  /// reader waiting. Since this is a concurrent queue the size is only a best
  /// effort guess until all reader and writer threads have been joined.
  [[nodiscard]] std::ptrdiff_t size() const noexcept {
    // TODO: How can we deal with wrapped queue on 32bit?
    return static_cast<std::ptrdiff_t>(head_.load(std::memory_order_relaxed) -
                                  tail_.load(std::memory_order_relaxed));
  }

  /// Returns true if the queue is empty.
  /// Since this is a concurrent queue this is only a best effort guess
  /// until all reader and writer threads have been joined.
  [[nodiscard]] bool empty() const noexcept { return size() <= 0; }

private:
  static void assign(T &destination, T &&item) noexcept {
    if constexpr (std::is_nothrow_move_assignable_v<T>) {
      destination = std::move(item);
    } else {
      destination = std::as_const(item);
    }
  }

  constexpr std::size_t idx(std::size_t i) const noexcept { return i % capacity_; }

  constexpr std::size_t turn(std::size_t i) const noexcept { return i / capacity_; }

private:
  void deallocate_slots() noexcept {
    SlotAllocatorTraits::deallocate(allocator_, slots_, capacity_ + 1);
  }

  const std::size_t capacity_;
  Slot *slots_;
  [[no_unique_address]] SlotAllocator allocator_;

  // Align to avoid false sharing between head_ and tail_
  alignas(kFalseSharingRange) std::atomic<std::size_t> head_;
  alignas(kFalseSharingRange) std::atomic<std::size_t> tail_;
};

} // namespace rigtorp
