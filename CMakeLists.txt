cmake_minimum_required(VERSION 3.15)
project(LumoraProject LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vulkan 라이브러리 찾기 (경로/환경 설정 필요)
find_package(Vulkan REQUIRED)

# 하위 디렉토리 빌드
add_subdirectory(LumoraRenderer)
add_subdirectory(LumoraTest)
