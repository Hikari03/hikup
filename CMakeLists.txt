cmake_minimum_required(VERSION 3.30)
project(hikup)

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Setting default build type to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DHIKUP_DEBUG -Wall -Wextra -g -pedantic -Werror")
set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} -Wall -Wextra -pedantic -Werror -O2")

# Include libsodium
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSODIUM REQUIRED libsodium)

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