cmake_minimum_required(VERSION 3.16)
project(CAS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
check_cxx_compiler_flag("-mfma" COMPILER_SUPPORTS_FMA)
if (COMPILER_SUPPORTS_AVX2)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
    message(STATUS "AVX2 SIMD support enabled (-mavx2)")
endif()
if (COMPILER_SUPPORTS_FMA)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfma")
endif()

find_program(LLVM_CONFIG_EXE NAMES llvm-config llvm-config-17)
if (LLVM_CONFIG_EXE)
    execute_process(COMMAND ${LLVM_CONFIG_EXE} --cxxflags OUTPUT_VARIABLE LLVM_CXXFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG_EXE} --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG_EXE} --libs core executionengine orcjit support native target OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG_EXE} --includedir OUTPUT_VARIABLE LLVM_INCDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    include_directories(${LLVM_INCDIR})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXXFLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LLVM_LDFLAGS}")
    add_definitions(-DHAS_LLVM_ORC_JIT=1)
    message(STATUS "LLVM ORC JIT found via ${LLVM_CONFIG_EXE}")
else()
    add_definitions(-DHAS_LLVM_ORC_JIT=0)
    set(LLVM_LIBS "")
    message(STATUS "LLVM not found, compiling with HAS_LLVM_ORC_JIT=0 fallback")
endif()

add_executable(cas
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/frontend/parser_expr.cpp
    src/frontend/parser_stmt.cpp
    src/frontend/parser_module.cpp
    src/kernel/elaborator.cpp
    src/kernel/evaluator.cpp
    src/kernel/dependency_graph.cpp
    src/kernel/bytecode_compiler.cpp
    src/kernel/codegen.cpp
    src/kernel/native_jit.cpp
    src/kernel/llvm_jit.cpp
    src/kernel/trace_buffer.cpp
    src/kernel/simd_ops.cpp
    src/kernel/memory_fastpath.cpp
    src/engine.cpp
    src/profiler/profiler.cpp
    src/tracer/vcd_dumper.cpp
    src/kernel/scheduler.cpp
    src/frontend/number_parser.cpp
    src/kernel/expr_evaluator.cpp
    src/kernel/execution_context.cpp
    src/frontend/parser_function.cpp
    src/frontend/parser_task.cpp
    src/main.cpp
)

target_link_libraries(cas ${CMAKE_DL_LIBS} ${LLVM_LIBS})
set_target_properties(cas PROPERTIES ENABLE_EXPORTS ON)
if(NOT MSVC)
    target_link_options(cas PRIVATE -rdynamic)
endif()
