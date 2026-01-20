cmake_minimum_required(VERSION 3.10)
project(my_compiler)

# C++17 표준 사용 (std::filesystem 등을 위해 필요)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LLVM 패키지 찾기
find_package(LLVM REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# LLVM 컴포넌트 설정
# 원래 작동하던 ${LLVM_TARGETS_TO_BUILD} 사용 방식으로 복구합니다.
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

# 실행 파일 생성
add_executable(my_compiler main.cpp)

# LLVM 라이브러리 링크
target_link_libraries(my_compiler PRIVATE ${llvm_libs})