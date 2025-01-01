#include "VulkanRenderer.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Lumora
{

    // 스태틱 함수 구현
    std::unique_ptr<IRenderer> IRenderer::Create()
    {
        return std::make_unique<VulkanRenderer>();
    }

    VulkanRenderer::VulkanRenderer()
    {
        InitVulkan();
    }

    VulkanRenderer::~VulkanRenderer()
    {
        CleanupVulkan();
    }

    //
    // ----- Vulkan 초기화 / 해제 ----
    //
    void VulkanRenderer::InitVulkan()
    {
        // 1. vk::Instance 생성
        vk::ApplicationInfo app_info;
        app_info.pApplicationName = "Lumora App";
        app_info.applicationVersion = 1;
        app_info.pEngineName = "Lumora Engine";
        app_info.engineVersion = 1;
        app_info.apiVersion = VK_API_VERSION_1_2;

        vk::InstanceCreateInfo instance_info;
        instance_info.pApplicationInfo = &app_info;

        // (플랫폼별 extension, validation layer 등 고려)
        instance_ = vk::createInstance(instance_info);

        // 2. Surface 생성 (플랫폼별 구현 필요)
        CreateSurface();

        // 3. 물리 디바이스 선택
        std::vector<vk::PhysicalDevice> pdevices = instance_.enumeratePhysicalDevices();
        if (pdevices.empty())
        {
            throw std::runtime_error("No Vulkan physical device found!");
        }
        physical_device_ = pdevices[0];

        // 4. 그래픽스 큐 인덱스 찾기
        std::vector<vk::QueueFamilyProperties> queue_families =
            physical_device_.getQueueFamilyProperties();
        for (uint32_t i = 0; i < queue_families.size(); ++i)
        {
            // 그래픽스 & 프레젠테이션 모두 지원하는 인덱스 찾기 (간단 처리)
            if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics)
            {
                if (physical_device_.getSurfaceSupportKHR(i, surface_))
                {
                    graphics_queue_index_ = i;
                    break;
                }
            }
        }

        // 5. vk::Device 생성
        float queue_priority = 1.0f;
        vk::DeviceQueueCreateInfo queue_info;
        queue_info.queueFamilyIndex = graphics_queue_index_;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &queue_priority;

        vk::DeviceCreateInfo device_create_info;
        device_create_info.queueCreateInfoCount = 1;
        device_create_info.pQueueCreateInfos = &queue_info;
        // (레이어, 익스텐션, 피처 등 고려)

        device_ = physical_device_.createDevice(device_create_info);
        graphics_queue_ = device_.getQueue(graphics_queue_index_, 0);

        // 6. 스왑체인 등 생성
        CreateSwapchain();
        CreateRenderPass();
        CreateFramebuffers();
        CreateCommandPoolAndBuffers();
        CreateSyncObjects();

        // 7. DescriptorPool, DescriptorSetLayout 생성
        CreateDescriptorPool();
        CreateDescriptorSetLayout();
    }

    void VulkanRenderer::CleanupVulkan()
    {
        // GPU 작업 완료 대기
        if (device_)
        {
            device_.waitIdle();
        }

        // 해제 순서: 프레임버퍼 -> 렌더패스 -> 스왑체인 등
        for (auto fb : framebuffers_)
        {
            device_.destroyFramebuffer(fb);
        }
        device_.destroyRenderPass(render_pass_);

        for (auto view : swapchain_image_views_)
        {
            device_.destroyImageView(view);
        }

        device_.destroySwapchainKHR(swapchain_);

        // 동기화 객체 해제
        for (int i = 0; i < kMaxFramesInFlight; ++i)
        {
            device_.destroySemaphore(image_available_semaphores_[i]);
            device_.destroySemaphore(render_finished_semaphores_[i]);
            device_.destroyFence(in_flight_fences_[i]);
        }

        // 커맨드 풀
        device_.destroyCommandPool(command_pool_);

        // Descriptor
        device_.destroyDescriptorSetLayout(descriptor_set_layout_);
        device_.destroyDescriptorPool(descriptor_pool_);

        // device
        if (device_)
        {
            device_.destroy();
        }
        // surface
        if (surface_)
        {
            instance_.destroySurfaceKHR(surface_);
        }
        // instance
        if (instance_)
        {
            instance_.destroy();
        }
    }

    //
    // ----- Surface / Swapchain / RenderPass / Framebuffer 등 -----
    //
    void VulkanRenderer::CreateSurface()
    {
        // 실제 윈도우(플랫폼)마다 다릅니다.
        // 여기서는 “이미 생성된 surface_가 있다고 가정”하거나,
        // 혹은 “dummy surface”를 생성한다고 가정합니다.
        // 예시로 GLFW 사용 시 glfwCreateWindowSurface(...)
        // Win32라면 vkCreateWin32SurfaceKHR(...)
        // ...
        // 본 예시에서는 surface_를 미리 만들어두었다고 가정합니다.
        // surface_ = ...
    }

    void VulkanRenderer::CreateSwapchain()
    {
        // 스왑체인 생성 (여기서는 매우 단순화, 실제로는
        // vkGetPhysicalDeviceSurfaceCapabilitiesKHR 등으로 크기, 포맷 결정)
        vk::SwapchainCreateInfoKHR swapchain_info;
        swapchain_info.surface = surface_;
        swapchain_info.minImageCount = 2; // double buffering
        swapchain_info.imageFormat = swapchain_format_;
        swapchain_info.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        swapchain_info.imageExtent = swapchain_extent_;
        swapchain_info.imageArrayLayers = 1;
        swapchain_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        swapchain_info.preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        swapchain_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapchain_info.presentMode = vk::PresentModeKHR::eFifo;
        swapchain_info.clipped = true;
        swapchain_info.imageSharingMode = vk::SharingMode::eExclusive;
        swapchain_info.queueFamilyIndexCount = 0;
        swapchain_info.pQueueFamilyIndices = nullptr;
        swapchain_info.oldSwapchain = nullptr;

        swapchain_ = device_.createSwapchainKHR(swapchain_info);
        swapchain_images_ = device_.getSwapchainImagesKHR(swapchain_);

        // ImageView
        swapchain_image_views_.resize(swapchain_images_.size());
        for (size_t i = 0; i < swapchain_images_.size(); ++i)
        {
            vk::ImageViewCreateInfo view_info;
            view_info.image = swapchain_images_[i];
            view_info.viewType = vk::ImageViewType::e2D;
            view_info.format = swapchain_format_;
            view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.layerCount = 1;

            swapchain_image_views_[i] = device_.createImageView(view_info);
        }
    }

    void VulkanRenderer::CreateRenderPass()
    {
        // 기본 컬러 어태치먼트
        vk::AttachmentDescription color_attach;
        color_attach.format = swapchain_format_;
        color_attach.samples = vk::SampleCountFlagBits::e1;
        color_attach.loadOp = vk::AttachmentLoadOp::eClear;
        color_attach.storeOp = vk::AttachmentStoreOp::eStore;
        color_attach.initialLayout = vk::ImageLayout::eUndefined;
        color_attach.finalLayout = vk::ImageLayout::ePresentSrcKHR;

        vk::AttachmentReference color_ref;
        color_ref.attachment = 0;
        color_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass;
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;

        vk::RenderPassCreateInfo rp_info;
        rp_info.attachmentCount = 1;
        rp_info.pAttachments = &color_attach;
        rp_info.subpassCount = 1;
        rp_info.pSubpasses = &subpass;

        // 서브패스 종결 후 레이아웃 전환 등
        vk::SubpassDependency dependency;
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = vk::AccessFlags{};
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        rp_info.dependencyCount = 1;
        rp_info.pDependencies = &dependency;

        render_pass_ = device_.createRenderPass(rp_info);
    }

    void VulkanRenderer::CreateFramebuffers()
    {
        framebuffers_.resize(swapchain_image_views_.size());
        for (size_t i = 0; i < swapchain_image_views_.size(); ++i)
        {
            vk::ImageView attachments[] = {swapchain_image_views_[i]};
            vk::FramebufferCreateInfo fb_info;
            fb_info.renderPass = render_pass_;
            fb_info.attachmentCount = 1;
            fb_info.pAttachments = attachments;
            fb_info.width = swapchain_extent_.width;
            fb_info.height = swapchain_extent_.height;
            fb_info.layers = 1;

            framebuffers_[i] = device_.createFramebuffer(fb_info);
        }
    }

    void VulkanRenderer::CreateCommandPoolAndBuffers()
    {
        vk::CommandPoolCreateInfo pool_info;
        pool_info.queueFamilyIndex = graphics_queue_index_;
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        command_pool_ = device_.createCommandPool(pool_info);

        // double buffering
        vk::CommandBufferAllocateInfo alloc_info;
        alloc_info.commandPool = command_pool_;
        alloc_info.level = vk::CommandBufferLevel::ePrimary;
        alloc_info.commandBufferCount = static_cast<uint32_t>(swapchain_images_.size());
        command_buffers_ = device_.allocateCommandBuffers(alloc_info);
    }

    void VulkanRenderer::CreateSyncObjects()
    {
        image_available_semaphores_.resize(kMaxFramesInFlight);
        render_finished_semaphores_.resize(kMaxFramesInFlight);
        in_flight_fences_.resize(kMaxFramesInFlight);

        vk::SemaphoreCreateInfo sem_info;
        vk::FenceCreateInfo fence_info;
        fence_info.flags = vk::FenceCreateFlagBits::eSignaled; // 초기 신호

        for (int i = 0; i < kMaxFramesInFlight; ++i)
        {
            image_available_semaphores_[i] = device_.createSemaphore(sem_info);
            render_finished_semaphores_[i] = device_.createSemaphore(sem_info);
            in_flight_fences_[i] = device_.createFence(fence_info);
        }
    }

    //
    // ----- DescriptorPool / DescriptorSetLayout -----
    //
    void VulkanRenderer::CreateDescriptorPool()
    {
        // 간단히 uniform-buffer 100개, combined-image-sampler 100개 정도
        std::vector<vk::DescriptorPoolSize> pool_sizes = {
            {vk::DescriptorType::eUniformBuffer, 100},
            {vk::DescriptorType::eCombinedImageSampler, 100},
        };

        vk::DescriptorPoolCreateInfo pool_info;
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        pool_info.maxSets = 100; // 최대 100개
        descriptor_pool_ = device_.createDescriptorPool(pool_info);
    }

    void VulkanRenderer::CreateDescriptorSetLayout()
    {
        // binding=0 -> uniform buffer, binding=1 -> combined image sampler
        vk::DescriptorSetLayoutBinding ubo_binding;
        ubo_binding.binding = 0;
        ubo_binding.descriptorType = vk::DescriptorType::eUniformBuffer;
        ubo_binding.descriptorCount = 1;
        ubo_binding.stageFlags = vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutBinding sampler_binding;
        sampler_binding.binding = 1;
        sampler_binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        sampler_binding.descriptorCount = 1;
        sampler_binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            ubo_binding, sampler_binding};

        vk::DescriptorSetLayoutCreateInfo layout_info;
        layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
        layout_info.pBindings = bindings.data();

        descriptor_set_layout_ = device_.createDescriptorSetLayout(layout_info);
    }

    //
    // ----- 간단 파이프라인(셰이더) 생성 로직 -----
    //
    PipelineHandle VulkanRenderer::CreatePipeline(const PipelineDesc &desc)
    {
        // 1. 셰이더 모듈 생성
        std::vector<char> vert_code = ReadFile(desc.vertex_shader_path);
        std::vector<char> frag_code = ReadFile(desc.fragment_shader_path);

        vk::ShaderModule vert_module = CreateShaderModule(vert_code);
        vk::ShaderModule frag_module = CreateShaderModule(frag_code);

        vk::PipelineShaderStageCreateInfo vert_stage;
        vert_stage.stage = vk::ShaderStageFlagBits::eVertex;
        vert_stage.module = vert_module;
        vert_stage.pName = "main"; // 엔트리포인트

        vk::PipelineShaderStageCreateInfo frag_stage;
        frag_stage.stage = vk::ShaderStageFlagBits::eFragment;
        frag_stage.module = frag_module;
        frag_stage.pName = "main";

        vk::PipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

        // 2. (예시) 고정 기능: Vertex Input(생략), InputAssembly 등
        vk::PipelineVertexInputStateCreateInfo vertex_input;
        vk::PipelineInputAssemblyStateCreateInfo input_assembly;
        input_assembly.topology = vk::PrimitiveTopology::eTriangleList;

        // 3. Viewport/Scissor
        vk::Viewport viewport(0.0f, 0.0f,
                              static_cast<float>(swapchain_extent_.width),
                              static_cast<float>(swapchain_extent_.height),
                              0.0f, 1.0f);
        vk::Rect2D scissor({0, 0}, swapchain_extent_);

        vk::PipelineViewportStateCreateInfo viewport_state;
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;

        // 4. Rasterizer
        vk::PipelineRasterizationStateCreateInfo rasterizer;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.lineWidth = 1.0f;

        // 5. Multisample
        vk::PipelineMultisampleStateCreateInfo multisample;
        multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // 6. ColorBlend
        vk::PipelineColorBlendAttachmentState colorblend_attach;
        colorblend_attach.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        colorblend_attach.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorblend;
        colorblend.attachmentCount = 1;
        colorblend.pAttachments = &colorblend_attach;

        // 7. Pipeline Layout (DescriptorSetLayout 1개 사용)
        vk::PipelineLayoutCreateInfo layout_info;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &descriptor_set_layout_;

        VulkanPipeline pipeline_obj;
        pipeline_obj.pipeline_layout = device_.createPipelineLayout(layout_info);

        // 8. 실제 Graphics Pipeline 생성
        vk::GraphicsPipelineCreateInfo pipeline_info;
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = shader_stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pColorBlendState = &colorblend;
        pipeline_info.layout = pipeline_obj.pipeline_layout;
        pipeline_info.renderPass = render_pass_;
        pipeline_info.subpass = 0;

        auto result = device_.createGraphicsPipeline(nullptr, pipeline_info);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Failed to create graphics pipeline");
        }
        pipeline_obj.pipeline = result.value;

        // 셰이더 모듈 해제 (파이프라인 생성 후 해제 가능)
        device_.destroyShaderModule(vert_module);
        device_.destroyShaderModule(frag_module);

        // 핸들 등록
        PipelineHandle handle = next_pipeline_handle_++;
        if (handle >= pipelines_.size())
        {
            pipelines_.resize(handle + 1);
        }
        pipelines_[handle] = pipeline_obj;

        return handle;
    }

    void VulkanRenderer::BindPipeline(PipelineHandle handle)
    {
        // 실제로는 command buffer 기록 시점에 vkCmdBindPipeline
        // 여기서는 간단히 “활성화할 파이프라인 핸들을 기억” 정도로 처리 가능
    }

    //
    // ----- 셰이더 로딩 헬퍼 -----
    //
    std::vector<char> VulkanRenderer::ReadFile(const std::string &filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }
        size_t file_size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(file_size);
        file.seekg(0);
        file.read(buffer.data(), file_size);
        file.close();
        return buffer;
    }

    vk::ShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char> &code)
    {
        vk::ShaderModuleCreateInfo create_info;
        create_info.codeSize = code.size();
        create_info.pCode = reinterpret_cast<const uint32_t *>(code.data());
        return device_.createShaderModule(create_info);
    }

    //
    // ----- 커맨드 버퍼 레코딩 (삼각형 그리기 예시) -----
    //
    void VulkanRenderer::RecordCommandBuffer(vk::CommandBuffer cmd,
                                             uint32_t image_index)
    {
        vk::CommandBufferBeginInfo begin_info;
        cmd.begin(begin_info);

        // 렌더패스 시작
        vk::RenderPassBeginInfo rp_begin;
        rp_begin.renderPass = render_pass_;
        rp_begin.framebuffer = framebuffers_[image_index];
        rp_begin.renderArea.offset = vk::Offset2D{0, 0};
        rp_begin.renderArea.extent = swapchain_extent_;

        vk::ClearValue clear_color = vk::ClearColorValue(std::array<float, 4>{0.1f, 0.2f, 0.3f, 1.0f});
        rp_begin.clearValueCount = 1;
        rp_begin.pClearValues = &clear_color;

        cmd.beginRenderPass(rp_begin, vk::SubpassContents::eInline);

        // 여기서 vkCmdBindPipeline, vkCmdBindDescriptorSets, vkCmdBindVertexBuffers,
        // vkCmdBindIndexBuffer, vkCmdDrawIndexed 등 실제 드로우 로직 수행
        // (BindPipeline, BindBuffer, BindTexture, BindSampler 등)

        cmd.endRenderPass();
        cmd.end();
    }

    //
    // ----- Frame(렌더링) 루프 ----
    //
    void VulkanRenderer::BeginFrame()
    {
        device_.waitForFences(in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
        device_.resetFences(in_flight_fences_[current_frame_]);

        // 스왑체인 이미지 인덱스 획득
        auto [result, image_index] = device_.acquireNextImageKHR(
            swapchain_, UINT64_MAX, image_available_semaphores_[current_frame_], nullptr);

        // 커맨드 버퍼 기록
        command_buffers_[image_index].reset();
        RecordCommandBuffer(command_buffers_[image_index], image_index);

        // 파이프라인 바인딩 등을 IRenderer 수준의 BindXXX로 대체 가능
    }

    void VulkanRenderer::EndFrame()
    {
        // 큐에 제출
        vk::SubmitInfo submit_info;

        vk::Semaphore wait_semaphores[] = {image_available_semaphores_[current_frame_]};
        vk::PipelineStageFlags wait_stages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_stages;

        submit_info.commandBufferCount = 1;
        // 여기서 현재 beginFrame()에서 사용된 image_index를 다시 가져와야하지만,
        // 예시이므로 단순 처리(실제로는 멤버 변수에 저장)
        // ...
        // 예: command_buffers_[image_index]
        submit_info.pCommandBuffers = &command_buffers_[0]; // 데모 목적

        vk::Semaphore signal_semaphores[] = {render_finished_semaphores_[current_frame_]};
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        graphics_queue_.submit(submit_info, in_flight_fences_[current_frame_]);

        // 프레젠트
        vk::PresentInfoKHR present_info;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;
        vk::SwapchainKHR swapchains[] = {swapchain_};
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swapchains;
        uint32_t image_index = 0; // 실제로는 beginFrame()에서 얻은 image_index
        present_info.pImageIndices = &image_index;

        graphics_queue_.presentKHR(present_info);

        current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
    }

    //
    // ----- 버퍼 / 텍스처 / 샘플러 / 리소스 해제 -----
    //
    BufferHandle VulkanRenderer::CreateBufferInternal(const BufferDesc &desc)
    {
        vk::BufferCreateInfo buffer_info;
        buffer_info.size = desc.size_in_bytes;

        vk::BufferUsageFlags usage_flags;
        if (desc.usage_uniform_buffer)
        {
            usage_flags |= vk::BufferUsageFlagBits::eUniformBuffer;
        }
        if (desc.usage_vertex_buffer)
        {
            usage_flags |= vk::BufferUsageFlagBits::eVertexBuffer;
        }
        if (desc.usage_index_buffer)
        {
            usage_flags |= vk::BufferUsageFlagBits::eIndexBuffer;
        }
        if (desc.usage_transfer_src)
        {
            usage_flags |= vk::BufferUsageFlagBits::eTransferSrc;
        }
        if (desc.usage_transfer_dst)
        {
            usage_flags |= vk::BufferUsageFlagBits::eTransferDst;
        }
        buffer_info.usage = usage_flags;

        VulkanBuffer vulkan_buffer;
        vulkan_buffer.buffer = device_.createBuffer(buffer_info);
        vulkan_buffer.size_in_bytes = desc.size_in_bytes;

        // VMA 할당/바인딩 로직(생략)

        // 핸들 등록
        BufferHandle handle = next_buffer_handle_++;
        if (handle >= buffers_.size())
        {
            buffers_.resize(handle + 1);
        }
        buffers_[handle] = vulkan_buffer;
        return handle;
    }

    BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc &desc)
    {
        return CreateBufferInternal(desc);
    }

    void VulkanRenderer::UpdateBuffer(BufferHandle handle, const void *data,
                                      size_t size)
    {
        if (handle == 0 || handle >= buffers_.size())
        {
            throw std::runtime_error("Invalid buffer handle in UpdateBuffer.");
        }
        // VMA 사용 시: vmaMapMemory(...), memcpy, vmaUnmapMemory(...)
        // (본 예시는 생략)
    }

    void VulkanRenderer::BindBuffer(BufferHandle handle, uint32_t bind_point)
    {
        // DescriptorSet 업데이트 or vkCmdBindVertexBuffers / vkCmdBindIndexBuffer
        // 등으로 연결.
    }

    TextureHandle VulkanRenderer::CreateTexture(const TextureDesc &desc,
                                                const void *initial_data)
    {
        vk::ImageCreateInfo image_info;
        image_info.imageType = vk::ImageType::e2D;
        image_info.extent.width = desc.width;
        image_info.extent.height = desc.height;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = vk::Format::eR8G8B8A8Unorm;
        image_info.usage =
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

        VulkanTexture vulkan_texture;
        vulkan_texture.image = device_.createImage(image_info);
        vulkan_texture.width = desc.width;
        vulkan_texture.height = desc.height;

        // VMA 할당 & 바인딩 (생략)
        // ...

        vk::ImageViewCreateInfo view_info;
        view_info.image = vulkan_texture.image;
        view_info.viewType = vk::ImageViewType::e2D;
        view_info.format = image_info.format;
        view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        vulkan_texture.image_view = device_.createImageView(view_info);

        // initial_data 업로드 로직 등 (생략)

        // 핸들 등록
        TextureHandle handle = next_texture_handle_++;
        if (handle >= textures_.size())
        {
            textures_.resize(handle + 1);
        }
        textures_[handle] = vulkan_texture;
        return handle;
    }

    void VulkanRenderer::BindTexture(TextureHandle handle, uint32_t bind_point)
    {
        // DescriptorSet 업데이트 등
    }

    SamplerHandle VulkanRenderer::CreateSampler(const SamplerDesc &desc)
    {
        vk::SamplerCreateInfo sampler_info;
        sampler_info.magFilter = vk::Filter::eLinear;
        sampler_info.minFilter = vk::Filter::eLinear;
        sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
        sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
        sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;

        VulkanSampler vulkan_sampler;
        vulkan_sampler.sampler = device_.createSampler(sampler_info);

        SamplerHandle handle = next_sampler_handle_++;
        if (handle >= samplers_.size())
        {
            samplers_.resize(handle + 1);
        }
        samplers_[handle] = vulkan_sampler;
        return handle;
    }

    void VulkanRenderer::BindSampler(SamplerHandle handle, uint32_t bind_point)
    {
        // DescriptorSet 업데이트 등
    }

    void VulkanRenderer::ReleaseResource(uint64_t handle)
    {
        // 버퍼, 텍스처, 샘플러, 파이프라인 중 하나인지 구분 후 해제
        if (handle < buffers_.size() && buffers_[handle].buffer)
        {
            device_.destroyBuffer(buffers_[handle].buffer);
            buffers_[handle].buffer = nullptr;
            return;
        }
        if (handle < textures_.size() && textures_[handle].image)
        {
            device_.destroyImageView(textures_[handle].image_view);
            device_.destroyImage(textures_[handle].image);
            textures_[handle].image = nullptr;
            return;
        }
        if (handle < samplers_.size() && samplers_[handle].sampler)
        {
            device_.destroySampler(samplers_[handle].sampler);
            samplers_[handle].sampler = nullptr;
            return;
        }
        if (handle < pipelines_.size() && pipelines_[handle].pipeline)
        {
            device_.destroyPipeline(pipelines_[handle].pipeline);
            device_.destroyPipelineLayout(pipelines_[handle].pipeline_layout);
            pipelines_[handle].pipeline = nullptr;
            return;
        }
    }

} // namespace Lumora
