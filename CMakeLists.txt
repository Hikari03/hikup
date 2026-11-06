cmake_minimum_required(VERSION 3.25)
project(hikup)

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Setting default build type to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(HIKUP_STATIC "Build hikup statically" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DHIKUP_DEBUG -Wall -Wextra -g -pedantic -Werror")
set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} -Wall -Wextra -pedantic -Werror -O2")

# Check the flag status
if(HIKUP_STATIC)
    message(STATUS "Configuring for static build")

    # Disable shared libraries for dependencies
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    # Example: Apply -static flag to GCC/Clang
    if(UNIX AND NOT APPLE)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
        set(BUILD_SHARED_LIBS OFF)
        set(CMAKE_EXE_LINKER_FLAGS "-static")
    endif()

else()
    message(STATUS "Configuring for dynamic build")
endif()

add_executable(hikup src/client/main.cpp
        src/shared/Connection.hpp
        src/shared/Connection.cpp
        src/client/util.cpp
        src/client/CommandType.cpp
        src/client/Color.hpp
        src/shared/FileInfo.cpp
        src/shared/FileInfo.hpp
        src/shared/utils.cpp
        src/shared/utils.cpp
        src/shared/utils.hpp
        src/client/BatchHandlers.hpp
        src/client/BatchHandlers.cpp
        src/client/CommandHandlers.cpp
        src/client/CommandHandlers.hpp
        src/client/E2E.cpp
        src/client/E2E.hpp
        src/client/CommandType.hpp
        src/client/Style.hpp)

add_executable(hikup-server src/server/main.cpp
        src/server/terminal.cpp
        src/server/accepter.cpp
        src/server/ClientInfo.cpp
        src/server/ClientInfo.hpp
        src/server/ConnectionServer.cpp
        src/server/ConnectionServer.hpp
        src/shared/Connection.hpp
        src/shared/Connection.cpp
        src/server/HTTPFileServer.cpp
        src/server/HTTPFileServer.hpp
        src/server/WebPages.hpp
        src/server/includes/mongoose.c
        src/server/includes/mongoose.h
        src/server/includes/toml.hpp
        src/shared/FileInfo.cpp
        src/shared/FileInfo.hpp
        src/shared/utils.cpp
        src/shared/utils.cpp
        src/shared/utils.hpp
        src/server/ConnectionHandler.cpp
        src/server/ConnectionHandler.hpp
        src/server/Settings.hpp
        src/server/Settings.cpp
        src/server/utils.cpp
        src/server/utils.hpp
        src/server/FileTracker.cpp
        src/server/FileTracker.hpp
        src/server/CertGen.cpp
        src/server/CertGen.hpp)

find_package(OpenSSL REQUIRED)
target_link_libraries(hikup OpenSSL::SSL)
target_link_libraries(hikup-server OpenSSL::SSL)

find_package(CURL REQUIRED)   # server-side URL fetch for `hikup fetch`
target_link_libraries(hikup-server CURL::libcurl)

# libcurl.a references its private dependencies, which the CURL::libcurl
# target does not carry onto the link line — link them explicitly; in a
# HIKUP_STATIC build CMAKE_FIND_LIBRARY_SUFFIXES is ".a", so these resolve
# to the static archives
find_library(HIKUP_NGHTTP2_LIB nghttp2 REQUIRED)
find_library(HIKUP_CARES_LIB cares REQUIRED)
find_library(HIKUP_PSL_LIB psl REQUIRED)
find_library(HIKUP_Z_LIB z REQUIRED)
find_library(HIKUP_ZSTD_LIB zstd REQUIRED)
find_library(HIKUP_BROTLIDEC_LIB brotlidec REQUIRED)
find_library(HIKUP_BROTLICOMMON_LIB brotlicommon REQUIRED)
find_library(HIKUP_IDN2_LIB idn2 REQUIRED)
find_library(HIKUP_UNISTRING_LIB unistring REQUIRED)
target_link_libraries(hikup-server
    ${HIKUP_NGHTTP2_LIB}
    ${HIKUP_CARES_LIB}
    ${HIKUP_PSL_LIB}
    ${HIKUP_Z_LIB}
    ${HIKUP_ZSTD_LIB}
    ${HIKUP_BROTLIDEC_LIB}
    ${HIKUP_BROTLICOMMON_LIB}
    ${HIKUP_IDN2_LIB}
    ${HIKUP_UNISTRING_LIB})

include(GNUInstallDirs)

install(TARGETS hikup hikup-server
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES hikup-completion.bash
        DESTINATION ${CMAKE_INSTALL_DATADIR}/bash-completion/completions
        RENAME hikup)

# shared data root (created by the install rule below, owned by the
# installing user): the saved-servers list and the system-wide trust store
set(HIKUP_DATA_DIR "${CMAKE_INSTALL_FULL_DATADIR}/hikup")

install(CODE "
    execute_process(COMMAND id -un OUTPUT_VARIABLE CURRENT_USER OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND id -gn OUTPUT_VARIABLE CURRENT_GROUP OUTPUT_STRIP_TRAILING_WHITESPACE)
    file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${HIKUP_DATA_DIR}\")
    file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${HIKUP_DATA_DIR}/trusted-certs\")
    if(NOT EXISTS \"\$ENV{DESTDIR}${HIKUP_DATA_DIR}/servers\")
        file(TOUCH \"\$ENV{DESTDIR}${HIKUP_DATA_DIR}/servers\")
    endif()
    execute_process(COMMAND chown \"\${CURRENT_USER}:\${CURRENT_GROUP}\" \"\$ENV{DESTDIR}${HIKUP_DATA_DIR}\" \"\$ENV{DESTDIR}${HIKUP_DATA_DIR}/servers\" \"\$ENV{DESTDIR}${HIKUP_DATA_DIR}/trusted-certs\"
                    ERROR_QUIET OUTPUT_QUIET)
")
