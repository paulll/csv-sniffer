cmake_minimum_required(VERSION 3.13)
project(csv_bruteforcer)
set(CMAKE_CXX_STANDARD 20)

set(SOURCE_FILES src/main.cc)
add_executable(csv_bruteforcer ${SOURCE_FILES})
