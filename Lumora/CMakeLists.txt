cmake_minimum_required(VERSION 3.18)
project(Lumora LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Win32 빌드 시 VK_USE_PLATFORM_WIN32_KHR 디파인
if (WIN32)
    add_definitions(-DVK_USE_PLATFORM_WIN32_KHR)
endif()


# Vulkan
find_package(Vulkan REQUIRED)


# 라이브러리 프로젝트: 정적 or 동적
add_library(Lumora STATIC
    src/VulkanRenderer.cpp
    # VulkanRenderer.h는 자동 포함
)

# include 디렉토리 설정
target_include_directories(Lumora
    PUBLIC
        ../include  # IRenderer.h가 들어있는 경로
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
		../external/VulkanMemoryAllocator/include 
		../external/Vulkan-Hpp/include 
        ../external/xxHash
        ${Vulkan_INCLUDE_DIRS}
)

# Vulkan
target_link_libraries(Lumora PRIVATE Vulkan::Vulkan ${Vulkan_LIBRARIES})