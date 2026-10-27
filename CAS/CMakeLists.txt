cmake_minimum_required(VERSION 3.16)
project(CAS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(cas
    src/lexer.cpp
    src/parser.cpp
    src/engine.cpp
    src/profiler/profiler.cpp
    src/main.cpp
)