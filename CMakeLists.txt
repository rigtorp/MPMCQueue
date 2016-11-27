cmake_minimum_required(VERSION 3.2)
project(MPMCQueue)

set(CMAKE_EXPORT_COMPILE_COMMANDS "true")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
endif()

find_package(Threads REQUIRED)

add_executable(MPMCQueueExample MPMCQueueExample.cpp)
target_link_libraries(MPMCQueueExample ${CMAKE_THREAD_LIBS_INIT})

add_executable(MPMCQueueTest MPMCQueueTest.cpp)
target_link_libraries(MPMCQueueTest ${CMAKE_THREAD_LIBS_INIT})

enable_testing()
add_test(MPMCQueueTest MPMCQueueTest)
