cmake_minimum_required(VERSION 3.22)
project(Chanel_Event)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -pthread)

find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

add_executable(ChanelSet pebs_monitor/ChanelSet.cpp)
target_link_libraries(ChanelSet spdlog::spdlog fmt::fmt)

add_executable(PerfEvent pebs_monitor/PerfEvent.cpp)
