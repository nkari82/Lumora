cmake_minimum_required(VERSION 3.18)
project(LumoraRenderer LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vulkan
find_package(Vulkan REQUIRED)

# 이 예시에서 VMA는 단순히 Vulkan Memory Allocator 헤더만 필요하다고 가정
# 실제로는 VMA 소스(깃헙 다운로드)나 서브모듈로 추가해야 할 수도 있음.

# 소스 파일 목록
set(LUMORA_SOURCES
    IRenderer.h
    VulkanRenderer.h
    VulkanRenderer.cpp
)

add_library(LumoraRenderer STATIC ${LUMORA_SOURCES})
target_include_directories(LumoraRenderer PUBLIC ${Vulkan_INCLUDE_DIRS})
target_link_libraries(LumoraRenderer PRIVATE ${Vulkan_LIBRARIES})

# 필요하다면, 컴파일 옵션/정적 분석/OS별 세부 옵션 등을 추가할 수 있습니다.
