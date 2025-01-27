// testVulkanRenderer.cpp
#include <Lumora/IRenderer.h>
#include <windows.h>

#include <iostream>
#include <thread>

static std::unique_ptr<lumora::IRenderer> renderer;

lumora::SwapChainHandle main_swapchain;
lumora::FrameBufferHandle main_framebuffer;

// Window Procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE: {
            UINT width = LOWORD(lParam);   // 새로운 너비
            UINT height = HIWORD(lParam);  // 새로운 높이

            switch (wParam) {
                case SIZE_MINIMIZED:
                    break;
                case SIZE_MAXIMIZED:
                case SIZE_RESTORED:
                    if (renderer)
                        renderer->Resize(main_swapchain, width, height);
                    break;
            }
            return 0;
        }
        case WM_SIZING: {
            LPRECT rect = (LPRECT)lParam;

            // 창의 최소 크기 제한
            const int minWidth = 300;
            const int minHeight = 200;

            if ((rect->right - rect->left) < minWidth)
                rect->right = rect->left + minWidth;

            if ((rect->bottom - rect->top) < minHeight)
                rect->bottom = rect->top + minHeight;

            UINT width = rect->right - rect->left;
            UINT height = rect->bottom - rect->top;

            if (renderer)
                renderer->Resize(main_swapchain, width, height);

            return TRUE;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return TRUE;
}

#define test_p

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

    lumora::WindowHandle wh = {reinterpret_cast<void*>(hwnd), reinterpret_cast<void*>(hInstance)};

    // Create Renderer
    renderer = lumora::IRenderer::Create();
    try {
        renderer->Open("Vulkan Renderer Test", wh);
    } catch (std::exception& err) {
        std::cout << err.what();
    }

    // Create SwapChain
    lumora::SwapChainDesc swap_desc;
    swap_desc.window_handle = wh;
    swap_desc.width = 1280;
    swap_desc.height = 720;
    swap_desc.color_format = lumora::Format::kB8G8R8A8Unorm;
    swap_desc.depth_format = lumora::Format::kD32SfloatS8Uint;
    swap_desc.buffer_count = 2;
    swap_desc.vsync = true;

    main_swapchain = renderer->CreateSwapChain(swap_desc);
    main_framebuffer = renderer->CreateFrameBuffer(main_swapchain);

    // init shader (#TODO 리플렉션이 제대로 되나 확인)
    auto vert_handle = renderer->CreateShader({lumora::ShaderStage::kVertex, "shaders/spv/test2.vert.spv"});
    auto frag_handle = renderer->CreateShader({lumora::ShaderStage::kFragment, "shaders/spv/test2.frag.spv"});

    lumora::PipelineDesc desc;
    desc.framebuffer = main_framebuffer;
    desc.vertex_shader = vert_handle;
    desc.fragment_shader = frag_handle;
    // desc.viewport.width = swapDesc.width;
    // desc.viewport.height = swapDesc.height;

#if defined(test_p)
    auto pl_handle = renderer->CreatePipeline(desc);
#endif

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
        renderer->Render(main_swapchain, [&](uint32_t width, uint32_t height) {
            // Begin Render Pass
            // lumora::RenderPassDesc passDesc;
            // Setup passDesc as needed
            renderer->BeginPass(main_framebuffer, {0, 0, (float)width, (float)height}, {0, 0, width, height});

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

    renderer->WaitIdle();
#if defined(test_p)
    renderer->ReleaseResource(pl_handle);
#endif
    renderer->ReleaseResource(vert_handle);
    renderer->ReleaseResource(frag_handle);
    renderer->ReleaseResource(main_swapchain);
    renderer->ReleaseResource(main_framebuffer);

    renderer->Close();

    return 0;
}
