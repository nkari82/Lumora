#include <Lumora/IRenderer.h>
#include <Windows.h>

#include <iostream>

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

    // 2. Renderer
    std::unique_ptr<Lumora::IRenderer> renderer = Lumora::IRenderer::Create();
    if (!renderer) {
        MessageBox(nullptr, TEXT("Failed to create Lumora Renderer"), TEXT("Error"), MB_OK);
        return 1;
    }

    // 3. SwapChain
    Lumora::SwapChainDesc sc_desc;
    sc_desc.window_handle = hwnd;
    sc_desc.width = 1280;
    sc_desc.height = 720;
    sc_desc.vsync = true;
    auto sc_handle = renderer->CreateSwapChain(sc_desc);

    // 4. 예시로 버퍼/셰이더/파이프라인 생성
    Lumora::ShaderDesc vs_desc{"vert.spv"};
    Lumora::ShaderDesc fs_desc{"frag.spv"};
    auto vs_handle = renderer->CreateShader(vs_desc);
    auto fs_handle = renderer->CreateShader(fs_desc);

    Lumora::PipelineDesc pdesc;
    pdesc.vertex_shader = vs_handle;
    pdesc.fragment_shader = fs_handle;
    auto pipeline_handle = renderer->CreatePipeline(pdesc);

    // TODO: 유니폼 버퍼, 텍스처, 정점/인덱스 버퍼 생성 -> UpdateBuffer -> 등등

    // 메시지 루프
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

        // 프레임
        renderer->BeginFrame();
        // draw call
        renderer->EndFrame();
    }

    // 리소스 해제
    renderer->ReleaseResource(pipeline_handle);
    renderer->ReleaseShader(vs_handle);
    renderer->ReleaseShader(fs_handle);
    renderer->ReleaseResource(sc_handle);

    return 0;
}
