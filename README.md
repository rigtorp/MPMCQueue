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

## Usage

- `MPMCQueue<T>(size_t capacity);`

  Constructs a new `MPMCQueue` holding items of type `T` with capacity
  `capacity`.
  
- `void emplace(Args &&... args);`

  Enqueue an item using inplace construction. Blocks if queue is full.
  
- `bool try_emplace(Args &&... args);`

  Try to enqueue an item using inplace construction. Returns `true` on
  success and `false` if queue is full.

- `bool push(const T &v);`

  Enqueue an item using copy construction. Blocks if queue is full.

- `template <typename P> bool push(P &&v);`

  Enqueue an item using move construction. Participates in overload
  resolution only if `std::is_nothrow_constructible<T, P&&>::value ==
  true`. Blocks if queue is full.

- `bool try_push(const T &v);`

  Try to enqueue an item using copy construction. Returns `true` on
  success and `false` if queue is full.

- `template <typename P> bool try_push(P &&v);`

  Try to enqueue an item using move construction. Participates in
  overload resolution only if `std::is_nothrow_constructible<T,
  P&&>::value == true`. Returns `true` on success and `false` if queue
  is full.

- `void pop(T &v);`

  Dequeue an item by copying or moving the item into `v`. Blocks if
  queue is empty.
  
- `bool try_pop(T &v);`

  Try to dequeue an item by copying or moving the item into
  `v`. Return `true` on sucess and `false` if the queue is empty.

All operations except construction and destruction are thread safe.

## Implementation

![Memory layout](https://github.com/rigtorp/MPMCQueue/blob/master/mpmc.png)

Enqeue:

1. Acquire next write *ticket* from *head*.
2. Wait for our *turn* (2 * (ticket / capacity)) to write *slot* (ticket % capacity).
3. Set *turn = turn + 1* to inform the readers we are done writing.

Dequeue:

1. Acquire next read *ticket* from *tail*.
2. Wait for our *turn* (2 * (ticket / capacity) + 1) to read *slot* (ticket % capacity).
3. Set *turn = turn + 1* to inform the writers we are done reading.


References:

- *Dave Dice*. [PTLQueue : a scalable bounded-capacity MPMC queue](https://blogs.oracle.com/dave/entry/ptlqueue_a_scalable_bounded_capacity).
- *Dmitry Vyukov*. [Bounded MPMC queue](http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue).
- *Massimiliano Meneghin et al*. [Performance evaluation of inter-thread communication mechanisms on multicore/multithreaded architectures](http://researcher.watson.ibm.com/researcher/files/ie-pasetto_davide/PerfLocksQueues.pdf).
- *Oleksandr Otenko*. [US 8607249 B2: System and method for efficient concurrent queue implementation](http://www.google.com/patents/US8607249).
- *Paul E. McKenney*. [Memory Barriers: a Hardware View for Software Hackers](http://irl.cs.ucla.edu/~yingdi/web/paperreading/whymb.2010.06.07c.pdf).

## Testing

Testing lock-free algorithms is hard. I'm using two approaches to test
the implementation:

- A single threaded test that the functionality works as intended,
  including that the element constructor and destructor is invoked
  correctly.
- A multithreaded fuzz test that all elements are enqueued and
  dequeued correctly under heavy contention.

## Benchmarks

TODO

## About

This project was created by [Erik Rigtorp](http://rigtorp.se)
<[erik@rigtorp.se](mailto:erik@rigtorp.se)>.
