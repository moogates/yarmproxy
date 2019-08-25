cmake_minimum_required(VERSION 2.8.9)
project(mcproxy)

# set (CMAKE_BUILD_TYPE "Debug")
set (CMAKE_BUILD_TYPE "Release")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11")
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -O0 -g -ggdb")
set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O2")

include_directories(..)
include_directories(/usr/local/include)
# add_definitions("-Wall -std=c++11") 

file(GLOB SOURCES "*.cc")
#set(SOURCES src/main.cc src/proxy_server.cc)
link_directories(../base/build)
link_directories(/usr/local/lib)

add_executable(mcproxy ${SOURCES})

target_link_libraries(mcproxy memc_base boost_system boost_thread pthread dl)

