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
        vk::ApplicationInfo app_info;
        app_info.pApplicationName = "Lumora App";
        app_info.applicationVersion = 1;
        app_info.pEngineName = "Lumora Engine";
        app_info.engineVersion = 1;
        app_info.apiVersion = VK_API_VERSION_1_2;

        vk::InstanceCreateInfo instance_info;
        instance_info.pApplicationInfo = &app_info;
        instance_ = vk::createInstance(instance_info);

        // Surface 생성 (플랫폼별 구현 필요)
        CreateSurface();

        // 물리 디바이스 선택
        auto pdevices = instance_.enumeratePhysicalDevices();
        if (pdevices.empty())
        {
            throw std::runtime_error("No Vulkan physical device found!");
        }
        physical_device_ = pdevices[0];

        // 그래픽스 큐 인덱스 찾기
        auto qfprops = physical_device_.getQueueFamilyProperties();
        for (uint32_t i = 0; i < qfprops.size(); ++i)
        {
            if (qfprops[i].queueFlags & vk::QueueFlagBits::eGraphics)
            {
                if (physical_device_.getSurfaceSupportKHR(i, surface_))
                {
                    graphics_queue_index_ = i;
                    break;
                }
            }
        }

        float qPriority = 1.0f;
        vk::DeviceQueueCreateInfo dqinfo;
        dqinfo.queueFamilyIndex = graphics_queue_index_;
        dqinfo.queueCount = 1;
        dqinfo.pQueuePriorities = &qPriority;

        vk::DeviceCreateInfo dci;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &dqinfo;

        device_ = physical_device_.createDevice(dci);
        graphics_queue_ = device_.getQueue(graphics_queue_index_, 0);

        // 스왑체인 등
        CreateSwapchain();
        CreateRenderPass();
        CreateFramebuffers();
        CreateCommandPoolAndBuffers();
        CreateSyncObjects();
        CreateDescriptorPool();
        CreateDescriptorSetLayout();
    }

    void VulkanRenderer::CleanupVulkan()
    {
        if (device_)
        {
            device_.waitIdle();
        }

        // 리소스 정리
        for (auto &fb : framebuffers_)
        {
            device_.destroyFramebuffer(fb);
        }
        device_.destroyRenderPass(render_pass_);

        for (auto &view : swapchain_image_views_)
        {
            device_.destroyImageView(view);
        }
        device_.destroySwapchainKHR(swapchain_);

        for (int i = 0; i < kMaxFramesInFlight; ++i)
        {
            device_.destroySemaphore(image_available_semaphores_[i]);
            device_.destroySemaphore(render_finished_semaphores_[i]);
            device_.destroyFence(in_flight_fences_[i]);
        }
        device_.destroyCommandPool(command_pool_);

        device_.destroyDescriptorSetLayout(descriptor_set_layout_);
        device_.destroyDescriptorPool(descriptor_pool_);

        // 파이프라인 해제(남아있다면)
        for (size_t i = 1; i < pipelines_.size(); ++i)
        {
            if (pipelines_[i].pipeline)
            {
                device_.destroyPipeline(pipelines_[i].pipeline);
            }
            if (pipelines_[i].pipeline_layout)
            {
                device_.destroyPipelineLayout(pipelines_[i].pipeline_layout);
            }
        }

        // 버퍼 해제
        for (size_t i = 1; i < buffers_.size(); ++i)
        {
            if (buffers_[i].buffer)
            {
                device_.destroyBuffer(buffers_[i].buffer);
            }
        }

        // 텍스처 해제
        for (size_t i = 1; i < textures_.size(); ++i)
        {
            if (textures_[i].image_view)
            {
                device_.destroyImageView(textures_[i].image_view);
            }
            if (textures_[i].image)
            {
                device_.destroyImage(textures_[i].image);
            }
        }

        // 샘플러 해제
        for (size_t i = 1; i < samplers_.size(); ++i)
        {
            if (samplers_[i].sampler)
            {
                device_.destroySampler(samplers_[i].sampler);
            }
        }

        // 셰이더 해제
        for (auto &kv : shaders_)
        {
            if (kv.second.shader_module)
            {
                device_.destroyShaderModule(kv.second.shader_module);
            }
        }
        shaders_.clear();

        if (device_)
        {
            device_.destroy();
        }
        if (surface_)
        {
            instance_.destroySurfaceKHR(surface_);
        }
        if (instance_)
        {
            instance_.destroy();
        }
    }

    void VulkanRenderer::CreateSurface()
    {
        // 예시: GLFW, Win32 등 플랫폼별로 구현 필요
        // surface_ = ...
    }

    void VulkanRenderer::CreateSwapchain()
    {
        vk::SwapchainCreateInfoKHR sci;
        sci.surface = surface_;
        sci.minImageCount = 2;
        sci.imageFormat = swapchain_format_;
        sci.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        sci.imageExtent = swapchain_extent_;
        sci.imageArrayLayers = 1;
        sci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        sci.imageSharingMode = vk::SharingMode::eExclusive;
        sci.preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        sci.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        sci.presentMode = vk::PresentModeKHR::eFifo;
        sci.clipped = true;

        swapchain_ = device_.createSwapchainKHR(sci);
        swapchain_images_ = device_.getSwapchainImagesKHR(swapchain_);

        swapchain_image_views_.resize(swapchain_images_.size());
        for (size_t i = 0; i < swapchain_images_.size(); ++i)
        {
            vk::ImageViewCreateInfo ivci;
            ivci.image = swapchain_images_[i];
            ivci.viewType = vk::ImageViewType::e2D;
            ivci.format = swapchain_format_;
            ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            ivci.subresourceRange.levelCount = 1;
            ivci.subresourceRange.layerCount = 1;
            swapchain_image_views_[i] = device_.createImageView(ivci);
        }
    }

    void VulkanRenderer::CreateRenderPass()
    {
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

        vk::RenderPassCreateInfo rpci;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &color_attach;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &subpass;

        // 서브패스 종결 후
        vk::SubpassDependency dependency;
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = {};
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        rpci.dependencyCount = 1;
        rpci.pDependencies = &dependency;

        render_pass_ = device_.createRenderPass(rpci);
    }

    void VulkanRenderer::CreateFramebuffers()
    {
        framebuffers_.resize(swapchain_image_views_.size());
        for (size_t i = 0; i < swapchain_image_views_.size(); ++i)
        {
            vk::FramebufferCreateInfo fci;
            fci.renderPass = render_pass_;
            vk::ImageView attachments[] = {swapchain_image_views_[i]};
            fci.attachmentCount = 1;
            fci.pAttachments = attachments;
            fci.width = swapchain_extent_.width;
            fci.height = swapchain_extent_.height;
            fci.layers = 1;
            framebuffers_[i] = device_.createFramebuffer(fci);
        }
    }

    void VulkanRenderer::CreateCommandPoolAndBuffers()
    {
        vk::CommandPoolCreateInfo cpci;
        cpci.queueFamilyIndex = graphics_queue_index_;
        cpci.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        command_pool_ = device_.createCommandPool(cpci);

        vk::CommandBufferAllocateInfo cbai;
        cbai.commandPool = command_pool_;
        cbai.level = vk::CommandBufferLevel::ePrimary;
        cbai.commandBufferCount = static_cast<uint32_t>(swapchain_image_views_.size());

        command_buffers_ = device_.allocateCommandBuffers(cbai);
    }

    void VulkanRenderer::CreateSyncObjects()
    {
        image_available_semaphores_.resize(kMaxFramesInFlight);
        render_finished_semaphores_.resize(kMaxFramesInFlight);
        in_flight_fences_.resize(kMaxFramesInFlight);

        vk::SemaphoreCreateInfo sci;
        vk::FenceCreateInfo fci;
        fci.flags = vk::FenceCreateFlagBits::eSignaled;

        for (int i = 0; i < kMaxFramesInFlight; ++i)
        {
            image_available_semaphores_[i] = device_.createSemaphore(sci);
            render_finished_semaphores_[i] = device_.createSemaphore(sci);
            in_flight_fences_[i] = device_.createFence(fci);
        }
    }

    void VulkanRenderer::CreateDescriptorPool()
    {
        std::vector<vk::DescriptorPoolSize> pool_sizes = {
            {vk::DescriptorType::eUniformBuffer, 100},
            {vk::DescriptorType::eCombinedImageSampler, 100},
        };
        vk::DescriptorPoolCreateInfo dpci;
        dpci.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        dpci.pPoolSizes = pool_sizes.data();
        dpci.maxSets = 100;
        descriptor_pool_ = device_.createDescriptorPool(dpci);
    }

    void VulkanRenderer::CreateDescriptorSetLayout()
    {
        // binding=0 -> UBO, binding=1 -> sampler
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

        vk::DescriptorSetLayoutCreateInfo dsci;
        dsci.bindingCount = static_cast<uint32_t>(bindings.size());
        dsci.pBindings = bindings.data();

        descriptor_set_layout_ = device_.createDescriptorSetLayout(dsci);
    }

    //
    // ----- Frame Control -----
    //
    void VulkanRenderer::BeginFrame()
    {
        device_.waitForFences(in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
        device_.resetFences(in_flight_fences_[current_frame_]);

        auto [result, image_index] = device_.acquireNextImageKHR(
            swapchain_, UINT64_MAX, image_available_semaphores_[current_frame_], {});
        // 실제로는 result 체크 필요

        // 명령버퍼 초기화
        command_buffers_[image_index].reset();
        RecordCommandBuffer(command_buffers_[image_index], image_index);
    }

    void VulkanRenderer::EndFrame()
    {
        // 간단히 command_buffers_[0]만 제출하는 예시 (실제로는 image_index별)
        vk::SubmitInfo si;

        vk::Semaphore wait_semaphores[] = {image_available_semaphores_[current_frame_]};
        vk::PipelineStageFlags wait_stages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = wait_semaphores;
        si.pWaitDstStageMask = wait_stages;

        si.commandBufferCount = 1;
        si.pCommandBuffers = &command_buffers_[0]; // 데모 목적

        vk::Semaphore signal_semaphores[] = {render_finished_semaphores_[current_frame_]};
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = signal_semaphores;

        graphics_queue_.submit(si, in_flight_fences_[current_frame_]);

        // 프레젠트
        vk::PresentInfoKHR pi;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = signal_semaphores;
        vk::SwapchainKHR swapchains[] = {swapchain_};
        pi.swapchainCount = 1;
        pi.pSwapchains = swapchains;
        uint32_t image_index = 0; // 실제로는 BeginFrame()에서 얻은 image_index
        pi.pImageIndices = &image_index;
        graphics_queue_.presentKHR(pi);

        current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
    }

    void VulkanRenderer::RecordCommandBuffer(vk::CommandBuffer cmd, uint32_t image_index)
    {
        vk::CommandBufferBeginInfo begin_info;
        cmd.begin(begin_info);

        vk::RenderPassBeginInfo rp_begin;
        rp_begin.renderPass = render_pass_;
        rp_begin.framebuffer = framebuffers_[image_index];
        rp_begin.renderArea.offset = vk::Offset2D{0, 0};
        rp_begin.renderArea.extent = swapchain_extent_;

        vk::ClearValue clear_color = vk::ClearColorValue(std::array<float, 4>{0.2f, 0.3f, 0.4f, 1.0f});
        rp_begin.clearValueCount = 1;
        rp_begin.pClearValues = &clear_color;

        cmd.beginRenderPass(rp_begin, vk::SubpassContents::eInline);

        // vkCmdBindPipeline, vkCmdBindDescriptorSets, vkCmdBindVertexBuffers,
        // vkCmdBindIndexBuffer, vkCmdDrawIndexed 등 수행

        cmd.endRenderPass();
        cmd.end();
    }

    //
    // ----- 셰이더 로딩 -----
    //
    vk::ShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char> &code)
    {
        vk::ShaderModuleCreateInfo smci;
        smci.codeSize = code.size();
        smci.pCode = reinterpret_cast<const uint32_t *>(code.data());
        return device_.createShaderModule(smci);
    }

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

    //
    // ----- 셰이더 (단일) 생성/해제 -----
    //
    ShaderHandle VulkanRenderer::CreateShader(const ShaderDesc &desc)
    {
        auto code = ReadFile(desc.file_path);
        vk::ShaderModule sm = CreateShaderModule(code);

        VulkanShader vs;
        vs.shader_module = sm;

        ShaderHandle handle = next_shader_handle_++;
        shaders_[handle] = vs;
        return handle;
    }

    void VulkanRenderer::ReleaseShader(ShaderHandle handle)
    {
        auto it = shaders_.find(handle);
        if (it != shaders_.end())
        {
            if (it->second.shader_module)
            {
                device_.destroyShaderModule(it->second.shader_module);
            }
            shaders_.erase(it);
        }
    }

    //
    // ----- 파이프라인 생성 -----
    //
    PipelineHandle VulkanRenderer::CreatePipeline(const PipelineDesc &desc)
    {
        // 셰이더 모듈 획득
        auto vsh_it = shaders_.find(desc.vertex_shader);
        auto fsh_it = shaders_.find(desc.fragment_shader);
        if (vsh_it == shaders_.end() || fsh_it == shaders_.end())
        {
            throw std::runtime_error("Invalid shader handle in PipelineDesc.");
        }
        vk::ShaderModule vert_mod = vsh_it->second.shader_module;
        vk::ShaderModule frag_mod = fsh_it->second.shader_module;

        vk::PipelineShaderStageCreateInfo vert_stage;
        vert_stage.stage = vk::ShaderStageFlagBits::eVertex;
        vert_stage.module = vert_mod;
        vert_stage.pName = "main";

        vk::PipelineShaderStageCreateInfo frag_stage;
        frag_stage.stage = vk::ShaderStageFlagBits::eFragment;
        frag_stage.module = frag_mod;
        frag_stage.pName = "main";

        vk::PipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

        // 간단 고정 기능
        vk::PipelineVertexInputStateCreateInfo vi;
        vk::PipelineInputAssemblyStateCreateInfo ia;
        ia.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport(0.f, 0.f,
                              float(swapchain_extent_.width),
                              float(swapchain_extent_.height), 0.f, 1.f);
        vk::Rect2D scissor({0, 0}, swapchain_extent_);

        vk::PipelineViewportStateCreateInfo vp;
        vp.viewportCount = 1;
        vp.pViewports = &viewport;
        vp.scissorCount = 1;
        vp.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rs;
        rs.polygonMode = vk::PolygonMode::eFill;
        rs.cullMode = vk::CullModeFlagBits::eBack;
        rs.frontFace = vk::FrontFace::eCounterClockwise;
        rs.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo ms;
        ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineColorBlendAttachmentState cba;
        cba.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        cba.blendEnable = false;

        vk::PipelineColorBlendStateCreateInfo cb;
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;

        vk::PipelineLayoutCreateInfo plci;
        plci.setLayoutCount = 1;                    // 예제
        plci.pSetLayouts = &descriptor_set_layout_; // 예시로 하나만

        VulkanPipeline vpip;
        vpip.pipeline_layout = device_.createPipelineLayout(plci);

        vk::GraphicsPipelineCreateInfo gpci;
        gpci.stageCount = 2;
        gpci.pStages = stages;
        gpci.pVertexInputState = &vi;
        gpci.pInputAssemblyState = &ia;
        gpci.pViewportState = &vp;
        gpci.pRasterizationState = &rs;
        gpci.pMultisampleState = &ms;
        gpci.pColorBlendState = &cb;
        gpci.layout = vpip.pipeline_layout;
        gpci.renderPass = render_pass_;
        gpci.subpass = 0;

        auto res = device_.createGraphicsPipeline(nullptr, gpci);
        if (res.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Failed to create pipeline.");
        }
        vpip.pipeline = res.value;

        PipelineHandle handle = next_pipeline_handle_++;
        if (handle >= pipelines_.size())
        {
            pipelines_.resize(handle + 1);
        }
        pipelines_[handle] = vpip;
        return handle;
    }

    void VulkanRenderer::BindPipeline(PipelineHandle handle)
    {
        // 실제로는 vkCmdBindPipeline을 해야 하므로
        // RecordCommandBuffer 시점에서 이 handle을 사용
    }

    //
    // ----- 버퍼 / 텍스처 / 샘플러 / 리소스 해제 -----
    //
    BufferHandle VulkanRenderer::CreateBufferInternal(const BufferDesc &desc)
    {
        vk::BufferCreateInfo bci;
        bci.size = desc.size_in_bytes;

        vk::BufferUsageFlags usage;
        if (desc.usage_uniform_buffer)
            usage |= vk::BufferUsageFlagBits::eUniformBuffer;
        if (desc.usage_vertex_buffer)
            usage |= vk::BufferUsageFlagBits::eVertexBuffer;
        if (desc.usage_index_buffer)
            usage |= vk::BufferUsageFlagBits::eIndexBuffer;
        if (desc.usage_transfer_src)
            usage |= vk::BufferUsageFlagBits::eTransferSrc;
        if (desc.usage_transfer_dst)
            usage |= vk::BufferUsageFlagBits::eTransferDst;
        bci.usage = usage;

        VulkanBuffer vb;
        vb.buffer = device_.createBuffer(bci);
        vb.size_in_bytes = desc.size_in_bytes;

        // VMA 할당 & bind 생략

        BufferHandle handle = next_buffer_handle_++;
        if (handle >= buffers_.size())
        {
            buffers_.resize(handle + 1);
        }
        buffers_[handle] = vb;
        return handle;
    }

    BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc &desc)
    {
        return CreateBufferInternal(desc);
    }

    void VulkanRenderer::UpdateBuffer(BufferHandle handle, const void *data, size_t size)
    {
        if (handle == 0 || handle >= buffers_.size())
        {
            throw std::runtime_error("Invalid buffer handle in UpdateBuffer.");
        }
        // VMA map & memcpy & unmap
    }

    void VulkanRenderer::BindBuffer(BufferHandle handle, uint32_t bind_point)
    {
        // DescriptorSet 업데이트 or vkCmdBindVertexBuffers / vkCmdBindIndexBuffer
    }

    TextureHandle VulkanRenderer::CreateTexture(const TextureDesc &desc,
                                                const void *initial_data)
    {
        vk::ImageCreateInfo ici;
        ici.imageType = vk::ImageType::e2D;
        ici.extent.width = desc.width;
        ici.extent.height = desc.height;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.format = vk::Format::eR8G8B8A8Unorm;
        ici.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

        VulkanTexture vt;
        vt.image = device_.createImage(ici);
        vt.width = desc.width;
        vt.height = desc.height;

        // VMA alloc & bind 생략

        vk::ImageViewCreateInfo ivci;
        ivci.image = vt.image;
        ivci.viewType = vk::ImageViewType::e2D;
        ivci.format = ici.format;
        ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;

        vt.image_view = device_.createImageView(ivci);

        // initial_data 업로드 로직 (staging buffer -> copy -> layout transition)

        TextureHandle handle = next_texture_handle_++;
        if (handle >= textures_.size())
        {
            textures_.resize(handle + 1);
        }
        textures_[handle] = vt;
        return handle;
    }

    void VulkanRenderer::BindTexture(TextureHandle handle, uint32_t bind_point)
    {
        // DescriptorSet 업데이트
    }

    SamplerHandle VulkanRenderer::CreateSampler(const SamplerDesc &desc)
    {
        vk::SamplerCreateInfo sci;
        sci.magFilter = vk::Filter::eLinear;
        sci.minFilter = vk::Filter::eLinear;
        sci.addressModeU = vk::SamplerAddressMode::eRepeat;
        sci.addressModeV = vk::SamplerAddressMode::eRepeat;
        sci.addressModeW = vk::SamplerAddressMode::eRepeat;

        VulkanSampler vs;
        vs.sampler = device_.createSampler(sci);

        SamplerHandle handle = next_sampler_handle_++;
        if (handle >= samplers_.size())
        {
            samplers_.resize(handle + 1);
        }
        samplers_[handle] = vs;
        return handle;
    }

    void VulkanRenderer::BindSampler(SamplerHandle handle, uint32_t bind_point)
    {
        // DescriptorSet 업데이트
    }

    //
    // ----- ReleaseResource -----
    //
    void VulkanRenderer::ReleaseResource(uint64_t handle)
    {
        // 버퍼
        if (handle < buffers_.size() && buffers_[handle].buffer)
        {
            device_.destroyBuffer(buffers_[handle].buffer);
            buffers_[handle].buffer = nullptr;
            return;
        }
        // 텍스처
        if (handle < textures_.size() && textures_[handle].image)
        {
            device_.destroyImageView(textures_[handle].image_view);
            device_.destroyImage(textures_[handle].image);
            textures_[handle].image = nullptr;
            return;
        }
        // 샘플러
        if (handle < samplers_.size() && samplers_[handle].sampler)
        {
            device_.destroySampler(samplers_[handle].sampler);
            samplers_[handle].sampler = nullptr;
            return;
        }
        // 파이프라인
        if (handle < pipelines_.size() && pipelines_[handle].pipeline)
        {
            device_.destroyPipeline(pipelines_[handle].pipeline);
            device_.destroyPipelineLayout(pipelines_[handle].pipeline_layout);
            pipelines_[handle].pipeline = nullptr;
            return;
        }
        // 셰이더 (단순히 ReleaseResource로도 해제 가능하도록)
        auto it = shaders_.find(handle);
        if (it != shaders_.end())
        {
            if (it->second.shader_module)
            {
                device_.destroyShaderModule(it->second.shader_module);
            }
            shaders_.erase(it);
            return;
        }
    }

} // namespace Lumora
