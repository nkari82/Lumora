project(Test LANGUAGES CXX)

add_executable(Test
    testLumora.cpp
)

# Win32 application (창 띄우기)
set_target_properties(Test PROPERTIES
    WIN32_EXECUTABLE ON
)

target_include_directories(Test PRIVATE ../external/glm ../include)

# LumoraRenderer 라이브러리 연결
target_link_libraries(Test
    PRIVATE
    Lumora
)