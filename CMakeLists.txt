cmake_minimum_required(VERSION 3.30)
project(hikup)

set(CMAKE_CXX_STANDARD 23)

# Include libsodium
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSODIUM libsodium)
add_executable(hikup src/main.cpp
        src/Connection.h
        src/Connection.cpp
        src/util.cpp
        src/CommandType.cpp
        src/Color.cpp)

add_executable(hikup-server server/main.cpp)

target_include_directories(hikup PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
target_link_libraries(hikup ${LIBSODIUM_LIBRARIES})