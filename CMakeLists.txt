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

add_executable(hikup-server server/main.cpp
        server/terminal.cpp
        server/accepter.cpp
        server/ClientInfo.cpp
        server/ClientInfo.h
        server/ConnectionServer.cpp
        server/ConnectionServer.h
        server/utilServer.cpp)

target_include_directories(hikup PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
target_link_libraries(hikup ${LIBSODIUM_LIBRARIES})

target_include_directories(hikup-server PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
target_link_libraries(hikup-server ${LIBSODIUM_LIBRARIES})