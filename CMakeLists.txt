cmake_minimum_required(VERSION 3.30)
project(hikup)

set(CMAKE_CXX_STANDARD 23)

add_executable(hikup server/main.cpp)
add_executable(hikup-server server/main.cpp)
