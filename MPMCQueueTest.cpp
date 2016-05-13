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

#include "MPMCQueue.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

// TestType has a global reference count and detects incorrect use of placement
// new
struct TestType {
  static int refCount;
  static constexpr uint64_t kMagic = 0x38c8fb6f8207a508;
  uint64_t magic;
  TestType() noexcept {
    refCount++;
    assert(magic != kMagic);
    magic = kMagic;
  };
  TestType(const TestType &) noexcept {
    refCount++;
    assert(magic != kMagic);
    magic = kMagic;
  };
  TestType(TestType &&) noexcept {
    refCount++;
    assert(magic != kMagic);
    magic = kMagic;
  };
  TestType &operator=(const TestType &other) noexcept {
    assert(magic == kMagic);
    assert(other.magic == kMagic);
    return *this;
  };
  TestType &operator=(TestType &&other) noexcept {
    assert(magic == kMagic);
    assert(other.magic == kMagic);
    return *this;
  }
  ~TestType() noexcept {
    refCount--;
    assert(magic == kMagic);
    magic = 0;
  };
};

int TestType::refCount = 0;

int main(int argc, char *argv[]) {

  using namespace rigtorp;

  {
    MPMCQueue<TestType> q(11);
    for (int i = 0; i < 10; i++) {
      q.emplace();
    }
    assert(TestType::refCount == 10);

    TestType t;
    q.pop(t);
    assert(TestType::refCount == 10);

    q.pop(t);
    q.emplace();
    assert(TestType::refCount == 10);
  }
  assert(TestType::refCount == 0);

  {
    MPMCQueue<int> q(1);
    int t = 0;
    assert(q.try_push(1) == true);
    assert(q.try_push(2) == false);
    assert(q.try_pop(t) == true && t == 1);
    assert(q.try_pop(t) == false && t == 1);
  }

  // Fuzz test
  {
    const uint64_t numOps = 1000;
    const int numThreads = 10;
    MPMCQueue<int> q(numThreads);
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
          int v;
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
