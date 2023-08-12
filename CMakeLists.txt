cmake_minimum_required(VERSION 3.13)
project(csv_bruteforcer)

set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

set(SOURCE_FILES src/main.cc)
add_executable(csv_bruteforcer ${SOURCE_FILES})

find_package(fmt)
target_link_libraries(csv_bruteforcer fmt::fmt)