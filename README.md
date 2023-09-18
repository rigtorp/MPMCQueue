# MPMCQueue.h

![C/C++ CI](https://github.com/rigtorp/MPMCQueue/workflows/C/C++%20CI/badge.svg)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/rigtorp/MPMCQueue/master/LICENSE)

A bounded multi-producer multi-consumer concurrent queue written in C++11.

It's battle hardened and used daily in production:
- In the [Frostbite game engine](https://www.ea.com/frostbite) developed by
  [Electronic Arts](https://www.ea.com/) for the following games:
  - [Anthem (2019)](https://www.ea.com/games/anthem)
  - [Battlefield V (2018)](https://www.ea.com/games/battlefield/battlefield-5)
  - [FIFA 18 (2017)](https://www.easports.com/fifa/fifa-18-cristiano-ronaldo)
  - [Madden NFL 18 (2017)](https://www.ea.com/games/madden-nfl/madden-nfl-18)
  - [Need for Speed: Payback (2017)](https://www.ea.com/games/need-for-speed/need-for-speed-payback)
- In the low latency trading infrastructure at [Charlesworth
  Research](https://www.charlesworthresearch.com/) and [Marquette
  Partners](https://www.marquettepartners.com/).

It's been cited by the following papers:
- Peizhao Ou and Brian Demsky. 2018. Towards understanding the costs of avoiding
  out-of-thin-air results. Proc. ACM Program. Lang. 2, OOPSLA, Article 136
  (October 2018), 29 pages. DOI: https://doi.org/10.1145/3276506 

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

- `void push(const T &v);`

  Enqueue an item using copy construction. Blocks if queue is full.

- `template <typename P> void push(P &&v);`

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

- `ssize_t size();`

  Returns the number of elements in the queue.

  The size can be negative when the queue is empty and there is at least one
  reader waiting. Since this is a concurrent queue the size is only a best
  effort guess until all reader and writer threads have been joined.

- `bool empty();`

  Returns true if the queue is empty.

  Since this is a concurrent queue this is only a best effort guess until all
  reader and writer threads have been joined.

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

- *Daniel Orozco, Elkin Garcia, Rishi Khan, Kelly Livingston, and Guang R. Gao*. 2012. Toward high-throughput algorithms on many-core architectures. ACM Trans. Archit. Code Optim. 8, 4, Article 49 (January 2012), 21 pages. DOI: https://doi.org/10.1145/2086696.2086728
- *Dave Dice*. 2014. [PTLQueue : a scalable bounded-capacity MPMC queue](https://blogs.oracle.com/dave/entry/ptlqueue_a_scalable_bounded_capacity).
- *Oleksandr Otenko*. [US 8607249 B2: System and method for efficient concurrent queue implementation](http://www.google.com/patents/US8607249).
- *Massimiliano Meneghin, Davide Pasetto, Hubertus Franke*. 2012. [Performance evaluation of inter-thread communication mechanisms on multicore/multithreaded architectures](http://researcher.watson.ibm.com/researcher/files/ie-pasetto_davide/PerfLocksQueues.pdf). DOI: https://doi.org/10.1145/2287076.2287098
- *Paul E. McKenney*. 2010. [Memory Barriers: a Hardware View for Software Hackers](http://irl.cs.ucla.edu/~yingdi/web/paperreading/whymb.2010.06.07c.pdf).
- *Dmitry Vyukov*. 2014. [Bounded MPMC queue](http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue).

## Testing

Testing concurrency algorithms is hard. I'm using two approaches to test the
implementation:

- A single threaded test that the functionality works as intended,
  including that the element constructor and destructor is invoked
  correctly.
- A multithreaded fuzz test that all elements are enqueued and
  dequeued correctly under heavy contention.

## TODO

- [X] Add allocator supports so that the queue could be used with huge pages and
  shared memory
- [ ] Add benchmarks and compare to `boost::lockfree::queue` and others
- [ ] Use C++20 concepts instead of `static_assert` if available
- [X] Use `std::hardware_destructive_interference_size` if available
- [ ] Add API for zero-copy deqeue and batch dequeue operations
- [ ] Add `[[nodiscard]]` attributes

## About

This project was created by [Erik Rigtorp](https://rigtorp.se)
<[erik@rigtorp.se](mailto:erik@rigtorp.se)>.
