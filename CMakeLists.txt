cmake_minimum_required(VERSION 3.16)
project(listtree VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(fmt REQUIRED)

add_executable(listtree src/main.cpp)
target_link_libraries(listtree PRIVATE fmt::fmt)
