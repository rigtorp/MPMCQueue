cmake_minimum_required(VERSION 3.2)
project(MPMCQueue CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS "true")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/permissive- /W4)
else()
	add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

add_library(MPMCQueue INTERFACE)
target_include_directories(MPMCQueue INTERFACE
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>)

find_package(Threads REQUIRED)

add_executable(MPMCQueueExample src/MPMCQueueExample.cpp)
target_link_libraries(MPMCQueueExample MPMCQueue Threads::Threads)

add_executable(MPMCQueueTest src/MPMCQueueTest.cpp)
target_link_libraries(MPMCQueueTest MPMCQueue Threads::Threads)

enable_testing()
add_test(MPMCQueueTest MPMCQueueTest)
