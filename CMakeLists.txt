cmake_minimum_required(VERSION 3.30)
project(hikup)

set(CMAKE_CXX_STANDARD 23)

add_executable(hikup src/main.cpp
        src/Connection.h
        src/Connection.cpp
        src/util.cpp
        src/CommandType.cpp
        src/Color.cpp)
add_executable(hikup-server server/main.cpp)
