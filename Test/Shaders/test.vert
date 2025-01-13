#version 450

// 입력: 위치 정보 (바인딩 위치 0)
layout(location = 0) in vec2 inPosition;

// 출력: 색상 정보
layout(location = 0) out vec3 outColor;

void main() {
    // 위치를 클립 공간으로 변환 (Z = 0, W = 1)
    gl_Position = vec4(inPosition, 0.0, 1.0);

    // 간단한 색상 계산 (정점 위치 기반으로 색상 생성)
    outColor = vec3(inPosition * 0.5 + 0.5, 1.0);
}

