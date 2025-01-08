// testVulkanRenderer.cpp
#include <Lumora/IRenderer.h>
#include <windows.h>

#include <thread>

// Window Procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Register Window Class
    const char CLASS_NAME[] = "Vulkan Window Class";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    // Create Window
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, "Vulkan Renderer Test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               1280, 720, nullptr, nullptr, hInstance, nullptr);

    if (hwnd == nullptr) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);

    // Create Renderer
    std::unique_ptr<lumora::IRenderer> renderer = lumora::IRenderer::Create();
    renderer->Open("Vulkan Renderer Test");

    lumora::WindowHandle wh = {reinterpret_cast<void*>(hwnd), reinterpret_cast<void*>(hInstance)};
    // Create SwapChain
    lumora::SwapChainDesc swapDesc;
    swapDesc.window_handle = wh;
    swapDesc.width = 1280;
    swapDesc.height = 720;
    swapDesc.color_format = lumora::Format::kSRGBA8Unorm;
    swapDesc.buffer_count = 2;
    swapDesc.vsync = true;

    lumora::SwapChainHandle swapchain = renderer->CreateSwapChain(swapDesc);
    lumora::FrameBufferHandle framebuffer = renderer->CreateFrameBuffer(swapchain);

    // Main Loop
    MSG msg = {};
    bool running = true;
    while (running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!running)
            break;

        // Rendering callback
        renderer->Render(swapchain, [&](uint32_t image_index) {
            // Begin Render Pass
            // lumora::RenderPassDesc passDesc;
            // Setup passDesc as needed
            renderer->BeginPass(framebuffer, image_index);

            // Bind pipeline, buffers, textures, etc.
            // For example:
            // renderer->BindPipeline(myPipeline);
            // renderer->BindBuffer(myVBO, 0);
            // renderer->BindBuffer(myIBO, 1);
            // renderer->DrawIndexed(36);

            // End Render Pass
            renderer->EndPass();
        });
    }

    renderer->Close();

    return 0;
}
