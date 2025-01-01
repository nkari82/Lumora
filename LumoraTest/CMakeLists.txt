project(LumoraTest LANGUAGES CXX)

add_executable(LumoraTest
    testLumora.cpp
)

# Win32 application (창 띄우기)
set_target_properties(LumoraTest PROPERTIES
    WIN32_EXECUTABLE ON
)

# LumoraRenderer 라이브러리 연결
target_link_libraries(LumoraTest
    PRIVATE
    LumoraRenderer
)
