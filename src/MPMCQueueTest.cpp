/*
Copyright (c) 2018 Erik Rigtorp <erik@rigtorp.se>

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

#undef NDEBUG

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <rigtorp/MPMCQueue.h>
#include <set>
#include <thread>
#include <vector>

// TestType tracks correct usage of constructors and destructors
struct TestType {
  static std::set<const TestType *> constructed;
  TestType() noexcept {
    assert(constructed.count(this) == 0);
    constructed.insert(this);
  };
  TestType(const TestType &other) noexcept {
    assert(constructed.count(this) == 0);
    assert(constructed.count(&other) == 1);
    constructed.insert(this);
  };
  TestType(TestType &&other) noexcept {
    assert(constructed.count(this) == 0);
    assert(constructed.count(&other) == 1);
    constructed.insert(this);
  };
  TestType &operator=(const TestType &other) noexcept {
    assert(constructed.count(this) == 1);
    assert(constructed.count(&other) == 1);
    return *this;
  };
  TestType &operator=(TestType &&other) noexcept {
    assert(constructed.count(this) == 1);
    assert(constructed.count(&other) == 1);
    return *this;
  }
  ~TestType() noexcept {
    assert(constructed.count(this) == 1);
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
    assert(state.count == 0);
    auto *allocation = std::allocator<T>{}.allocate(count);
    state.count = count;
    if (state.misalign) {
      auto *bytes = reinterpret_cast<std::byte *>(allocation);
      return static_cast<T *>(static_cast<void *>(bytes + 1));
    }
    return allocation;
  }

  void deallocate(T *allocation, std::size_t count) noexcept {
    assert(state.count == count);
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
    assert(q.size() == 0 && q.empty());
    for (int i = 0; i < 10; i++) {
      q.emplace();
    }
    assert(q.size() == 10 && !q.empty());
    assert(TestType::constructed.size() == 10);

    TestType t;
    q.pop(t);
    assert(q.size() == 9 && !q.empty());
    assert(TestType::constructed.size() == 10);

    q.pop(t);
    q.emplace();
    assert(q.size() == 9 && !q.empty());
    assert(TestType::constructed.size() == 10);
  }
  assert(TestType::constructed.size() == 0);

  {
    MPMCQueue<int> q(1);
    int t = 0;
    assert(q.try_push(1) == true);
    assert(q.size() == 1 && !q.empty());
    assert(q.try_push(2) == false);
    assert(q.size() == 1 && !q.empty());
    assert(q.try_pop(t) == true && t == 1);
    assert(q.size() == 0 && q.empty());
    assert(q.try_pop(t) == false && t == 1);
    assert(q.size() == 0 && q.empty());
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
    assert(q.try_emplace(v));
    q.push(v);
    assert(q.try_push(v));
    // xvalue
    q.push(Test());
    assert(q.try_push(Test()));
    q.pop(v);
    assert(q.try_pop(v));
  }

  // Movable only type
  {
    MPMCQueue<std::unique_ptr<int>> q(16);
    q.emplace(std::make_unique<int>(1));
    assert(q.try_emplace(std::make_unique<int>(2)));
    q.push(std::make_unique<int>(3));
    assert(q.try_push(std::make_unique<int>(4)));
    std::unique_ptr<int> v;
    q.pop(v);
    assert(v && *v == 1);
    assert(q.try_pop(v) && *v == 2);
    assert(q.try_consume([&v](std::unique_ptr<int> &&w) noexcept {
             v = std::move(w);
           }) == true);
    assert(*v == 3);
  }

  {
    struct Item {
      bool *consumed;
      void consume() && noexcept { *consumed = true; }
    };
    bool consumed = false;
    MPMCQueue<Item> q(1);
    assert(!q.try_consume(&Item::consume));
    q.emplace(&consumed);
    assert(q.try_consume(&Item::consume));
    assert(consumed && q.empty());

    q.emplace(&consumed);
    assert(q.try_consume(&Item::consumed));
    assert(q.empty());
  }

  // Check that pointer have same address and value after moving through queue
  {
    MPMCQueue<std::unique_ptr<int>> q(1);
    {
      auto up = std::make_unique<int>(1);
      auto *rp = up.get();
      std::unique_ptr<int> up2;
      assert(q.try_emplace(std::move(up)) == true);
      assert(q.try_consume([&](std::unique_ptr<int> v) noexcept {
        up2 = std::move(v);
      }) == true);
      assert(rp == up2.get());
      assert(*up2 == 1);
    }

    {
      auto up = std::make_unique<int>(1);
      auto *rp = up.get();
      std::unique_ptr<int> up2;
      q.emplace(std::move(up));
      assert(q.try_consume([&](std::unique_ptr<int> v) noexcept {
        up2 = std::move(v);
      }) == true);
      assert(rp == up2.get());
      assert(*up2 == 1);
    }

    {
      auto up = std::make_unique<int>(1);
      auto *rp = up.get();
      std::unique_ptr<int> up2;
      q.push(std::move(up));
      assert(q.try_consume([&](std::unique_ptr<int> v) noexcept {
        up2 = std::move(v);
      }) == true);
      assert(rp == up2.get());
      assert(*up2 == 1);
    }

    {
      auto up = std::make_unique<int>(1);
      auto *rp = up.get();
      std::unique_ptr<int> up2;
      assert(q.try_push(std::move(up)) == true);
      assert(q.try_consume([&](std::unique_ptr<int> v) noexcept {
        up2 = std::move(v);
      }) == true);
      assert(rp == up2.get());
      assert(*up2 == 1);
    }
  }

  {
    bool throws = false;
    try {
      MPMCQueue<int> q(0);
    } catch (std::exception &) {
      throws = true;
    }
    assert(throws == true);
  }

  {
    AllocationState state;
    {
      MPMCQueue<int, TestAllocator<int>> q(1, TestAllocator<int>(state));
      assert(state.count == 2);
      assert(q.try_push(42));
      int value = 0;
      assert(q.try_pop(value) && value == 42);
    }
    assert(state.count == 0);

    state.misalign = true;
    bool throws = false;
    try {
      MPMCQueue<int, TestAllocator<int>> q(1, TestAllocator<int>(state));
    } catch (const std::bad_alloc &) {
      throws = true;
    }
    assert(throws && state.count == 0);
  }

  // Fuzz test
  {
    const std::size_t numOps = 1000;
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
          q.push(static_cast<std::uint64_t>(j));
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
          q.pop(v);
          threadSum += v;
        }
        sum += threadSum;
      }));
    }
    flag = true;
    for (auto &thread : threads) {
      thread.join();
    }
    assert(sum == numOps * (numOps - 1) / 2);
  }

  return 0;
}
