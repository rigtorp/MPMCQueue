# MPMCQueue.h

[![Build Status](https://travis-ci.org/rigtorp/MPMCQueue.svg?branch=master)](https://travis-ci.org/rigtorp/MPMCQueue)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/rigtorp/MPMCQueue/master/LICENSE)

A bounded multi-producer multi-consumer lock-free queue written in
C++11.

## Example

```cpp
MPMCQueue<int> q(10);
auto t1 = std::thread([&] {
  int v;
  q.pop(v);
  std::cout << "t1 " << v << "\n";
});
auto t2 = std::thread([&] {
  int v;
  q.pop(v);
  std::cout << "t2 " << v << "\n";
});
q.push(1);
q.push(2);
t1.join();
t2.join();
```

## About

This project was created by [Erik Rigtorp](http://rigtorp.se)
<[erik@rigtorp.se](mailto:erik@rigtorp.se)>.
