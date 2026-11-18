// SPDX-FileCopyrightText: 2018 Erik Rigtorp <erik@rigtorp.se>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <rigtorp/MPMCQueue.h>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #expr     \
                << std::endl;                                                  \
      std::abort();                                                            \
    }                                                                          \
  } while (false)

// TestType tracks correct usage of constructors and destructors
struct TestType {
  static std::set<const TestType *> constructed;
  TestType() noexcept {
    CHECK(constructed.count(this) == 0);
    constructed.insert(this);
  };
  TestType(const TestType &other) noexcept {
    CHECK(constructed.count(this) == 0);
    CHECK(constructed.count(&other) == 1);
    constructed.insert(this);
  };
  TestType(TestType &&other) noexcept {
    CHECK(constructed.count(this) == 0);
    CHECK(constructed.count(&other) == 1);
    constructed.insert(this);
  };
  TestType &operator=(const TestType &other) noexcept {
    CHECK(constructed.count(this) == 1);
    CHECK(constructed.count(&other) == 1);
    return *this;
  };
  TestType &operator=(TestType &&other) noexcept {
    CHECK(constructed.count(this) == 1);
    CHECK(constructed.count(&other) == 1);
    return *this;
  }
  ~TestType() noexcept {
    CHECK(constructed.count(this) == 1);
    constructed.erase(this);
  };
  // To verify that alignment and padding calculations are handled correctly
  char data[129];
};

std::set<const TestType *> TestType::constructed;

struct AllocationState {
  std::size_t count = 0;
  bool misalign = false;
};

template <typename T> struct TestAllocator {
  using value_type = T;

  explicit TestAllocator(AllocationState &allocationState) noexcept
      : state(allocationState) {}

  template <typename U>
  TestAllocator(const TestAllocator<U> &other) noexcept : state(other.state) {}

  T *allocate(std::size_t count) {
    CHECK(state.count == 0);
    auto *allocation = std::allocator<T>{}.allocate(count);
    state.count = count;
    if (state.misalign) {
      auto *bytes = reinterpret_cast<std::byte *>(allocation);
      return static_cast<T *>(static_cast<void *>(bytes + 1));
    }
    return allocation;
  }

  void deallocate(T *allocation, std::size_t count) noexcept {
    CHECK(state.count == count);
    if (state.misalign) {
      auto *bytes = reinterpret_cast<std::byte *>(allocation);
      allocation = static_cast<T *>(static_cast<void *>(bytes - 1));
    }
    std::allocator<T>{}.deallocate(allocation, count);
    state.count = 0;
  }

  AllocationState &state;
};

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  using namespace rigtorp;

  {
    MPMCQueue<TestType> q(11);
    CHECK(q.size() == 0 && q.empty());
    for (int i = 0; i < 10; i++) {
      q.emplace();
    }
    CHECK(q.size() == 10 && !q.empty());
    CHECK(TestType::constructed.size() == 10);

    TestType t;
    q.pop(t);
    CHECK(q.size() == 9 && !q.empty());
    CHECK(TestType::constructed.size() == 10);

    q.pop(t);
    q.emplace();
    CHECK(q.size() == 9 && !q.empty());
    CHECK(TestType::constructed.size() == 10);
  }
  CHECK(TestType::constructed.size() == 0);

  {
    MPMCQueue<int> q(1);
    int t = 0;
    CHECK(q.try_push(1) == true);
    CHECK(q.size() == 1 && !q.empty());
    CHECK(q.try_push(2) == false);
    CHECK(q.size() == 1 && !q.empty());
    CHECK(q.try_pop(t) == true && t == 1);
    CHECK(q.size() == 0 && q.empty());
    CHECK(q.try_pop(t) == false && t == 1);
    CHECK(q.size() == 0 && q.empty());
  }

  // Copyable only type
  {
    struct alignas(512) Test {
      Test() = default;
      Test(const Test &) noexcept {}
      Test &operator=(const Test &) noexcept { return *this; }
      Test(Test &&) = delete;
      Test &operator=(Test &&) = delete;
    };
    MPMCQueue<Test> q(16);
    // lvalue
    Test v;
    q.emplace(v);
    CHECK(q.try_emplace(v));
    q.push(v);
    CHECK(q.try_push(v));
    // xvalue
    q.push(Test());
    CHECK(q.try_push(Test()));
    q.pop(v);
    CHECK(q.try_pop(v));
  }

  // Movable only type
  {
    MPMCQueue<std::unique_ptr<int>> q(16);
    q.emplace(std::make_unique<int>(1));
    CHECK(q.try_emplace(std::make_unique<int>(2)));
    q.push(std::make_unique<int>(3));
    CHECK(q.try_push(std::make_unique<int>(4)));
    std::unique_ptr<int> v;
    q.pop(v);
    CHECK(v && *v == 1);
    CHECK(q.try_pop(v) && *v == 2);
    CHECK(q.try_consume([&v](std::unique_ptr<int> &&w) noexcept {
      v = std::move(w);
    }) == true);
    CHECK(*v == 3);
  }

  {
    struct Item {
      bool *consumed;
      void consume() && noexcept { *consumed = true; }
    };
    bool consumed = false;
    MPMCQueue<Item> q(1);
    CHECK(!q.try_consume(&Item::consume));
    q.emplace(&consumed);
    CHECK(q.try_consume(&Item::consume));
    CHECK(consumed && q.empty());

    q.emplace(&consumed);
    CHECK(q.try_consume(&Item::consumed));
    CHECK(q.empty());
  }

  // Check that pointer have same address and value after moving through queue
  {
    MPMCQueue<std::unique_ptr<int>> q(1);
    {
      auto up = std::make_unique<int>(1);
      auto *rp = up.get();
      std::unique_ptr<int> up2;
      CHECK(q.try_emplace(std::move(up)) == true);
      CHECK(q.try_consume([&](std::unique_ptr<int> v) noexcept {
        up2 = std::move(v);
      }) == true);
      CHECK(rp == up2.get());
      CHECK(*up2 == 1);
    }

    {
      auto up = std::make_unique<int>(1);
      auto *rp = up.get();
      std::unique_ptr<int> up2;
      q.emplace(std::move(up));
      CHECK(q.try_consume([&](std::unique_ptr<int> v) noexcept {
        up2 = std::move(v);
      }) == true);
      CHECK(rp == up2.get());
      CHECK(*up2 == 1);
    }

    {
      auto up = std::make_unique<int>(1);
      auto *rp = up.get();
      std::unique_ptr<int> up2;
      q.push(std::move(up));
      CHECK(q.try_consume([&](std::unique_ptr<int> v) noexcept {
        up2 = std::move(v);
      }) == true);
      CHECK(rp == up2.get());
      CHECK(*up2 == 1);
    }

    {
      auto up = std::make_unique<int>(1);
      auto *rp = up.get();
      std::unique_ptr<int> up2;
      CHECK(q.try_push(std::move(up)) == true);
      CHECK(q.try_consume([&](std::unique_ptr<int> v) noexcept {
        up2 = std::move(v);
      }) == true);
      CHECK(rp == up2.get());
      CHECK(*up2 == 1);
    }
  }

  {
    bool throws = false;
    try {
      MPMCQueue<int> q(0);
    } catch (std::exception &) {
      throws = true;
    }
    CHECK(throws == true);
  }

  {
    AllocationState state;
    {
      MPMCQueue<int, TestAllocator<int>> q(1, TestAllocator<int>(state));
      CHECK(state.count == 2);
      CHECK(q.try_push(42));
      int value = 0;
      CHECK(q.try_pop(value) && value == 42);
    }
    CHECK(state.count == 0);

    state.misalign = true;
    bool throws = false;
    try {
      MPMCQueue<int, TestAllocator<int>> q(1, TestAllocator<int>(state));
    } catch (const std::bad_alloc &) {
      throws = true;
    }
    CHECK(throws && state.count == 0);
  }

  {
    bool throws = false;
    try {
      MPMCQueue<int> q(std::numeric_limits<std::size_t>::max());
    } catch (const std::bad_array_new_length &) {
      throws = true;
    }
    CHECK(throws);
  }

  // Exercise both ticket paths under contention, including try-operation
  // retries when another producer or consumer claims a ticket first.
  for (bool tryOperations : {false, true}) {
    const std::size_t numOps = 10000;
    const std::size_t numThreads = 10;
    MPMCQueue<std::uint64_t> q(numThreads);
    std::atomic<bool> flag(false);
    std::vector<std::thread> threads;
    std::atomic<std::uint64_t> sum(0);
    for (std::size_t i = 0; i < numThreads; ++i) {
      threads.push_back(std::thread([&, i] {
        while (!flag)
          ;
        for (auto j = i; j < numOps; j += numThreads) {
          if (tryOperations) {
            while (!q.try_push(static_cast<std::uint64_t>(j)))
              ;
          } else {
            q.push(static_cast<std::uint64_t>(j));
          }
        }
      }));
    }
    for (std::size_t i = 0; i < numThreads; ++i) {
      threads.push_back(std::thread([&, i] {
        while (!flag)
          ;
        std::uint64_t threadSum = 0;
        for (auto j = i; j < numOps; j += numThreads) {
          std::uint64_t v;
          if (tryOperations) {
            while (!q.try_pop(v))
              ;
          } else {
            q.pop(v);
          }
          threadSum += v;
        }
        sum += threadSum;
      }));
    }
    flag = true;
    for (auto &thread : threads) {
      thread.join();
    }
    CHECK(sum == numOps * (numOps - 1) / 2);
  }

  return 0;
}
