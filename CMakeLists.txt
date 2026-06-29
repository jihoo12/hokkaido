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

# Build the Rust cubical library as a static library
set(CUBICAL_LIB "${CMAKE_SOURCE_DIR}/target/release/libcubical_c.a")

# Custom target to build the Rust library
add_custom_target(cubical_lib ALL
    COMMAND cargo build --release
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building cubical Rust library..."
    BYPRODUCTS ${CUBICAL_LIB}
)

add_executable(my_compiler main.cpp)

# Add dependency on the cubical library
add_dependencies(my_compiler cubical_lib)

target_link_libraries(my_compiler PRIVATE
    ${llvm_libs}
    ${CUBICAL_LIB}
    pthread
    dl
    m
)
