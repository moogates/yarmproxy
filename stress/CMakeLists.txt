cmake_minimum_required(VERSION 2.8.9)
project(yarmproxy)
set (YARM_ROOT_DIR "..")

set (CMAKE_BUILD_TYPE "Debug")
#set (CMAKE_BUILD_TYPE "Release")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11")
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -O0 -g -ggdb")
set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O2")

include_directories(/usr/local/include)
include_directories(${YARM_ROOT_DIR})

add_definitions(-DLOGURU_WITH_STREAMS=1) 

aux_source_directory(. SOURCES)
aux_source_directory(${YARM_ROOT_DIR}/base BASE_SOURCES)
#file(GLOB SOURCES "*.cc")
#set(SOURCES main.cc redis_conn.cc)
link_directories(/usr/local/lib)

add_executable(yarmbench ${BASE_SOURCES} ${SOURCES} ../proxy/logging.cc ../proxy/loguru.cc)

target_link_libraries(yarmbench boost_system boost_thread pthread dl)

