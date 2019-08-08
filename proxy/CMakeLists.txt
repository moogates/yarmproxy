cmake_minimum_required(VERSION 2.8.9)
project(memc_proxy)

set (CMAKE_BUILD_TYPE "Debug")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11")
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -O0 -g -ggdb")
set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3")

include_directories(..)
# add_definitions("-Wall -std=c++11") 

file(GLOB SOURCES "*.cc")
#set(SOURCES src/main.cc src/proxy_server.cc)
link_directories(/home/muyuwei/memc_proxy/base/build)

add_executable(memc_proxy ${SOURCES})

target_link_libraries(memc_proxy /home/muyuwei/memc_proxy/base/build/libmemc_base.a boost_system boost_thread pthread dl)

