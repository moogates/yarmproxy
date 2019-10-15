cmake_minimum_required(VERSION 2.8.9)
project(yarmproxy)
set (YARM_ROOT_DIR "..")

#SET (CMAKE_CXX_COMPILER "clang++")
SET (CMAKE_CXX_COMPILER "g++")

#set (CMAKE_BUILD_TYPE "Debug")
set (CMAKE_BUILD_TYPE "Release")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11")
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -O0 -g -ggdb")
set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O2")

include_directories(/usr/local/include)
include_directories(${YARM_ROOT_DIR})

add_definitions(-DLOGURU_WITH_STREAMS=1) 

aux_source_directory(. SOURCES)
#file(GLOB SOURCES "*.cc")
#set(SOURCES src/main.cc src/proxy_server.cc)
link_directories(/usr/local/lib)
message(${SOURCES})

add_executable(yarmproxy ${SOURCES})

target_link_libraries(yarmproxy boost_system boost_thread pthread dl)

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  MESSAGE(STATUS "this is Linux")
  # target_link_libraries(yarmproxy atomic)
endif (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")

