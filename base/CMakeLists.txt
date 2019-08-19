cmake_minimum_required(VERSION 2.8.9)
project(memc_base)

set (CMAKE_BUILD_TYPE "Debug")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11")
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -O0 -g -ggdb")
set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O2")

include_directories(/usr/local/include)

add_definitions(-DLOGURU_WITH_STREAMS=1) 

file(GLOB SOURCES "*.cc")
#set(SOURCES src/main.cc src/proxy_server.cc)

add_library(memc_base STATIC ${SOURCES})
target_link_libraries(memc_base dl)

