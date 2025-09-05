cmake_minimum_required(VERSION 3.18)
project(qml-agent-bridge LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_subdirectory(bridge-core)
add_subdirectory(examples/minimal-cpp)

add_subdirectory(tools/cli)
