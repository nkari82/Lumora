cmake_minimum_required(VERSION 3.18)
project(Lumora LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vulkan
find_package(Vulkan REQUIRED)

# SPIRV-Cross 추가
set(SPIRV_CROSS_ROOT external/SPIRV-Cross)
add_subdirectory(${SPIRV_CROSS_ROOT})

# 라이브러리 프로젝트: 정적(STATIC) or 동적(SHARED)
add_library(Lumora SHARED src/VulkanRenderer.cpp)

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
        ${SPIRV_CROSS_ROOT}
        ${SPIRV_CROSS_ROOT}/include
)

# Win32 빌드 시 VK_USE_PLATFORM_WIN32_KHR 디파인
if (WIN32)
    target_compile_definitions(Lumora PRIVATE VK_USE_PLATFORM_WIN32_KHR LUMORA_EXPORTS) #or add_definitions(-DVK_USE_PLATFORM_WIN32_KHR)
    #target_compile_options(Lumora PRIVATE -Wall)
endif()

# Vulkan
target_link_libraries(Lumora PRIVATE 
    Vulkan::Vulkan 
    spirv-cross-core
    spirv-cross-glsl
    spirv-cross-reflect
    )