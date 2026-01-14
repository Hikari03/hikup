cmake_minimum_required(VERSION 3.25)
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

add_executable(hikup src/client/main.cpp
        src/client/Connection.hpp
        src/client/Connection.cpp
        src/client/util.cpp
        src/client/CommandType.cpp
        src/client/Color.cpp
        src/shared/FileInfo.cpp
        src/shared/FileInfo.hpp
        src/shared/utils.cpp
        src/shared/utils.cpp
        src/shared/utils.hpp)

add_executable(hikup-server src/server/main.cpp
        src/server/terminal.cpp
        src/server/accepter.cpp
        src/server/ClientInfo.cpp
        src/server/ClientInfo.hpp
        src/server/ConnectionServer.cpp
        src/server/ConnectionServer.hpp
        src/server/utilServer.cpp
        src/server/HTTPFileServer.cpp
        src/server/HTTPFileServer.hpp
        src/server/includes/mongoose.cpp
        src/server/includes/mongoose.hpp
        src/server/includes/toml.hpp
        src/shared/FileInfo.cpp
        src/shared/FileInfo.hpp
        src/shared/utils.cpp
        src/shared/utils.cpp
        src/shared/utils.hpp)

target_include_directories(hikup PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
target_link_libraries(hikup ${LIBSODIUM_LIBRARIES})

target_include_directories(hikup-server PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
target_link_libraries(hikup-server ${LIBSODIUM_LIBRARIES})