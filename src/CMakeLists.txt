cmake_minimum_required(VERSION 2.8.10)

set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

PROJECT(cronet_conn_stat)

FILE(GLOB Main_SRC_FILES 
    "cronet_conn_stat.cpp")

SET(SRC_FILES ${Main_SRC_FILES})

# add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)
include_directories(.)
include_directories(../include)
link_directories(../lib/mac/arm64)
#link_directories(../lib/win/x64)

set(LIBPATH_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(cronet_conn_stat ${SRC_FILES})

target_link_libraries(cronet_conn_stat
    # rt # for shm_open/close/...
        netbase
        stdc++
        )

