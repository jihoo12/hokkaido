cmake_minimum_required(VERSION 3.10)
project(my_compiler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(llvm_libs
    support
    core
    irreader
    codegen
    target
    mc
    mcparser
    asmparser
    option
    ${LLVM_TARGETS_TO_BUILD}
)

add_executable(my_compiler main.cpp)

target_link_libraries(my_compiler PRIVATE ${llvm_libs})