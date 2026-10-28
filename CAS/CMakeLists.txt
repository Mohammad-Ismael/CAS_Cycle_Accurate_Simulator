cmake_minimum_required(VERSION 3.16)
project(CAS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(cas
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/frontend/parser_expr.cpp
    src/frontend/parser_stmt.cpp
    src/frontend/parser_module.cpp
    src/kernel/elaborator.cpp
    src/kernel/evaluator.cpp
    src/engine.cpp
    src/profiler/profiler.cpp
    src/tracer/vcd_dumper.cpp
    src/kernel/scheduler.cpp
    src/frontend/number_parser.cpp
    src/main.cpp
)