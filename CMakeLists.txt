cmake_minimum_required(VERSION 2.8)
project(MPMCQueue)

set(CMAKE_EXPORT_COMPILE_COMMANDS "true")
set(CMAKE_CXX_FLAGS "-std=c++11 -Wall")

link_libraries(pthread rt)

add_executable(MPMCQueueTest MPMCQueueTest.cpp)

enable_testing()
add_test(MPMCQueueTest MPMCQueueTest)
