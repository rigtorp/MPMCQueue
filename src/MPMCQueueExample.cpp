#include <iostream>
#include <rigtorp/MPMCQueue.h>
#include <thread>

int main(int argc, char *argv[]) {
  (void)argc, (void)argv;

  using namespace rigtorp;

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

  return 0;
}
