#include <Windows.h>

#include <iostream>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../LumoraRenderer/include/IRenderer.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // 1. Win32 Window
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = TEXT("LumoraWinClass");
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(0, wc.lpszClassName, TEXT("Lumora Test Window"), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                               CW_USEDEFAULT, 1280, 720, nullptr, nullptr, hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    // 2. Lumora Renderer
    // 추가로, 유니폼 버퍼/정점 버퍼/인덱스 버퍼 + 텍스처, 샘플러를 생성하여
    // IRenderer만 사용해 삼각형을 실제 그려보는 로직

    std::unique_ptr<Lumora::IRenderer> renderer = Lumora::IRenderer::Create();

    // SwapChain
    Lumora::SwapChainDesc sc_desc;
    sc_desc.window_handle = hwnd;
    sc_desc.width = 1280;
    sc_desc.height = 720;
    sc_desc.vsync = true;
    auto sc_handle = renderer->CreateSwapChain(sc_desc);

    // 셰이더
    Lumora::ShaderDesc vs_desc{"vert.spv"};
    Lumora::ShaderDesc fs_desc{"frag.spv"};
    auto vs_handle = renderer->CreateShader(vs_desc);
    auto fs_handle = renderer->CreateShader(fs_desc);

    // 파이프라인
    Lumora::PipelineDesc pdesc;
    pdesc.vertex_shader = vs_handle;
    pdesc.fragment_shader = fs_handle;
    auto pipeline_handle = renderer->CreatePipeline(pdesc);

    // 1) 정점 버퍼
    std::vector<float> vertices = {// px, py, u, v
                                   -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.5f, -0.5f, 1.0f, 1.0f};
    Lumora::BufferDesc vb_desc;
    vb_desc.size_in_bytes = vertices.size() * sizeof(float);
    vb_desc.usage_vertex_buffer = true;
    vb_desc.usage_transfer_dst = true;
    auto vb_handle = renderer->CreateBuffer(vb_desc);
    renderer->UpdateBuffer(vb_handle, vertices.data(), vb_desc.size_in_bytes);
    // 2) 인덱스 버퍼
    std::vector<uint16_t> indices = {0, 1, 2};
    Lumora::BufferDesc ib_desc;
    ib_desc.size_in_bytes = indices.size() * sizeof(uint16_t);
    ib_desc.usage_index_buffer = true;
    ib_desc.usage_transfer_dst = true;
    auto ib_handle = renderer->CreateBuffer(ib_desc);
    renderer->UpdateBuffer(ib_handle, indices.data(), ib_desc.size_in_bytes);
    // 3) 유니폼 버퍼
    Lumora::BufferDesc ubo_desc;
    ubo_desc.size_in_bytes = sizeof(glm::mat4);
    ubo_desc.usage_uniform_buffer = true;
    ubo_desc.usage_transfer_dst = true;
    auto ubo_handle = renderer->CreateBuffer(ubo_desc);
    // MVP
    glm::mat4 model = glm::mat4(1.f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(45.f), 1.77f, 0.1f, 10.f);
    proj[1][1] *= -1.f;
    glm::mat4 mvp = proj * view * model;
    renderer->UpdateBuffer(ubo_handle, &mvp, sizeof(mvp));
    // 4) 텍스처 + 샘플러
    Lumora::TextureDesc tex_desc;
    tex_desc.width = 256;
    tex_desc.height = 256;
    auto tex_handle = renderer->CreateTexture(tex_desc, nullptr);  // dummy
    Lumora::SamplerDesc samp_desc;
    auto samp_handle = renderer->CreateSampler(samp_desc);
    // 5) (중요) VulkanRenderer에 있는 "CreateTestDescriptorSet(...)" 호출해야 하나,
    //    여기서는 IRenderer에 그런 함수가 없으므로, 가정상 "BindXXX" 등으로 동적 업데이트하거나,
    //    "하드코딩"이라고 가정.
    //    실제로는, IRenderer 인터페이스에 descriptor set 업데이트 API 추가 필요.
    //    여기서는 예시로 "BindBuffer(...)" / "BindTexture(...)" 등만 호출 가정.
    renderer->BindBuffer(ubo_handle, /*bindPoint=*/2);  // UBO
    renderer->BindBuffer(vb_handle, /*bindPoint=*/0);   // VBO
    renderer->BindBuffer(ib_handle, /*bindPoint=*/1);   // IBO
    renderer->BindTexture(tex_handle, /*bindPoint=*/0);
    renderer->BindSampler(samp_handle, /*bindPoint=*/0);
    MSG msg = {};
    bool running = true;
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        renderer->BeginFrame();
        renderer->EndFrame();
    }
    // 자원 해제
    renderer->ReleaseResource(vb_handle);
    renderer->ReleaseResource(ib_handle);
    renderer->ReleaseResource(ubo_handle);
    renderer->ReleaseResource(tex_handle);
    renderer->ReleaseResource(samp_handle);
    renderer->ReleaseResource(pipeline_handle);
    renderer->ReleaseShader(vs_handle);
    renderer->ReleaseShader(fs_handle);
    renderer->ReleaseResource(sc_handle);
    return 0;
}
