#include <windows.h>
#include <iostream>
#include "../LumoraRenderer/include/IRenderer.h"

// 전역/정적
static const char *g_className = "LumoraWinClass";
static HWND g_hWnd = nullptr;
static bool g_bRunning = true;

// Win32 프로시저
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CLOSE:
    case WM_DESTROY:
        g_bRunning = false;
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// 간단 Win32 창 생성
HWND CreateTestWindow(HINSTANCE hInstance, int width, int height)
{
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = g_className;
    RegisterClass(&wc);

    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hWnd = CreateWindow(
        g_className,
        "Lumora Test Window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    ShowWindow(hWnd, SW_SHOW);
    return hWnd;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // 1. 윈도우 생성
    g_hWnd = CreateTestWindow(hInstance, 800, 600);
    if (!g_hWnd)
    {
        MessageBox(nullptr, "Failed to create window", "Error", MB_OK);
        return 1;
    }

    // 2. Lumora IRenderer 생성
    std::unique_ptr<Lumora::IRenderer> renderer = Lumora::IRenderer::Create();
    if (!renderer)
    {
        MessageBox(nullptr, "Failed to create Lumora Renderer", "Error", MB_OK);
        return 1;
    }

    // 3. 스왑체인 생성
    Lumora::SwapChainDesc sc_desc;
    sc_desc.window_handle = (void *)g_hWnd;
    sc_desc.width = 800;
    sc_desc.height = 600;
    sc_desc.vsync = true;

    Lumora::SwapChainHandle sc_handle = renderer->CreateSwapChain(sc_desc);

    // 4. 셰이더(간단 예시, 실제 SPIR-V 파일 필요)
    Lumora::ShaderDesc vert_desc{"triangle_vert.spv"};
    Lumora::ShaderDesc frag_desc{"triangle_frag.spv"};
    auto vert_shader = renderer->CreateShader(vert_desc);
    auto frag_shader = renderer->CreateShader(frag_desc);

    // 5. 파이프라인
    Lumora::PipelineDesc pipe_desc;
    pipe_desc.vertex_shader = vert_shader;
    pipe_desc.fragment_shader = frag_shader;
    auto pipeline = renderer->CreatePipeline(pipe_desc);

    // 6. 버퍼 / 텍스처 / 샘플러 ...
    // (생략) 유니폼버퍼, 인덱스버퍼 생성 등

    // 메시지 루프
    MSG msg;
    while (g_bRunning)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                g_bRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_bRunning)
            break;

        // 7. 프레임 렌더링
        renderer->BeginFrame();
        // ... draw call ...
        renderer->EndFrame();
    }

    // 리소스 해제
    renderer->ReleaseResource(pipeline);
    renderer->ReleaseShader(vert_shader);
    renderer->ReleaseShader(frag_shader);

    renderer->ReleaseResource(sc_handle);

    return 0;
}
