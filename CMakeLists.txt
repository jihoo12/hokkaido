cmake_minimum_required(VERSION 3.10)
project(MyCompiler)

# 시스템의 LLVM 패키지 찾기
find_package(LLVM REQUIRED CONFIG)

# LLVM 헤더 경로 추가
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(my_compiler main.cpp)

# LLVM 라이브러리 링크
llvm_map_components_to_libnames(llvm_libs core support)
target_link_libraries(my_compiler ${llvm_libs})
