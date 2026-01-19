cmake_minimum_required(VERSION 3.10)
project(my_compiler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LLVM 패키지 찾기
find_package(LLVM REQUIRED CONFIG)

# LLVM이 지원하는 모든 타겟 리스트를 가져옵니다.
# (이 변수에 X86, ARM 등의 리스트가 자동으로 담깁니다)
set(LLVM_TARGETS_TO_BUILD ${LLVM_TARGETS_TO_BUILD})

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# 개별 컴포넌트를 명시합니다. 
# all-targets 대신 ${LLVM_TARGETS_TO_BUILD} 변수를 사용합니다.
llvm_map_components_to_libnames(llvm_libs
    support
    core
    irreader
    codegen
    target
    mc
    mcparser
    option
    ${LLVM_TARGETS_TO_BUILD} 
)

add_executable(my_compiler main.cpp)

target_link_libraries(my_compiler PRIVATE ${llvm_libs})