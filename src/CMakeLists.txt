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
if(WIN32 OR CMAKE_SYSTEM_NAME MATCHES "MINGW")
    # mingw choked on this
    #if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    if (CMAKE_SIZEOF_VOID_P EQUAL 8) 
        link_directories(../lib/win/x64)
        message("windows x64")
    else()
        link_directories(../lib/win/x86)
        message("windows x86")
    endif()
elseif(APPLE)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        link_directories(../lib/mac/arm64)
        message("apple arm64")
    else()
        link_directories(../lib/mac/x86_64)
        message("apple x86_64")
    endif()
# not support now
#elseif(UNIX)
#   link_directories(../lib/linux/x64)
#   message("unix")
else()
    message("unknown")
    message("cmake system name: ${CMAKE_SYSTEM_NAME}")
    message("cmake host system name: ${CMAKE_HOST_SYSTEM_NAME}")
    message("cmake system processor: ${CMAKE_SYSTEM_PROCESSOR}")
    message("compiler target: ${COMPILER_TARGET}")
    message("void* size: ${CMAKE_SIZEOF_VOID_P}")
    message("msys: ${MSYS}")
    message("mingw: ${MINGW}")
endif()


set(LIBPATH_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(cronet_conn_stat ${SRC_FILES})

target_link_libraries(cronet_conn_stat
    # rt # for shm_open/close/...
        netbase
        stdc++
        )

