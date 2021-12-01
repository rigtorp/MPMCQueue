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
#include <chrono>
#include <iostream>
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

int main(int argc, char *argv[]) {
  (void)argc, (void)argv;

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
    struct Test {
      Test() {}
      Test(const Test &) noexcept {}
      Test &operator=(const Test &) noexcept { return *this; }
      Test(Test &&) = delete;
    };
    MPMCQueue<Test> q(16);
    // lvalue
    Test v;
    q.emplace(v);
    q.try_emplace(v);
    q.push(v);
    q.try_push(v);
    // xvalue
    q.push(Test());
    q.try_push(Test());
  }

  // Movable only type
  {
    MPMCQueue<std::unique_ptr<int>> q(16);
    // lvalue
    auto v = std::unique_ptr<int>(new int(1));
    // q.emplace(v);
    // q.try_emplace(v);
    // q.push(v);
    // q.try_push(v);
    // xvalue
    q.emplace(std::unique_ptr<int>(new int(1)));
    q.try_emplace(std::unique_ptr<int>(new int(1)));
    q.push(std::unique_ptr<int>(new int(1)));
    q.try_push(std::unique_ptr<int>(new int(1)));
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

  // Fuzz test
  {
    const uint64_t numOps = 1000;
    const uint64_t numThreads = 10;
    MPMCQueue<uint64_t> q(numThreads);
    std::atomic<bool> flag(false);
    std::vector<std::thread> threads;
    std::atomic<uint64_t> sum(0);
    for (uint64_t i = 0; i < numThreads; ++i) {
      threads.push_back(std::thread([&, i] {
        while (!flag)
          ;
        for (auto j = i; j < numOps; j += numThreads) {
          q.push(j);
        }
      }));
    }
    for (uint64_t i = 0; i < numThreads; ++i) {
      threads.push_back(std::thread([&, i] {
        while (!flag)
          ;
        uint64_t threadSum = 0;
        for (auto j = i; j < numOps; j += numThreads) {
          uint64_t v;
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
