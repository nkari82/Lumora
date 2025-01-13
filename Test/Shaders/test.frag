#version 450

// 입력: 버텍스 셰이더에서 전달된 색상 정보
layout(location = 0) in vec3 inColor;

// 출력: 최종 색상
layout(location = 0) out vec4 outFragColor;

void main() {
    // 픽셀 색상을 입력 색상으로 설정
    outFragColor = vec4(inColor, 1.0);
}