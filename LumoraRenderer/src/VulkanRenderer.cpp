#include "VulkanRenderer.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Lumora {

// 스태틱 함수
std::unique_ptr<IRenderer> IRenderer::Create() { return std::make_unique<VulkanRenderer>(); }

VulkanRenderer::VulkanRenderer() {}

VulkanRenderer::~VulkanRenderer() { CleanupVulkan(); }

void VulkanRenderer::InitVulkan() {
    if (initialized_)
        return;
    initialized_ = true;

    // 1) vkInstance
    vk::ApplicationInfo app_info;
    app_info.pApplicationName = "Lumora App";
    app_info.applicationVersion = 1;
    app_info.pEngineName = "Lumora Engine";
    app_info.engineVersion = 1;
    app_info.apiVersion = VK_API_VERSION_1_2;

    vk::InstanceCreateInfo ici;
    ici.pApplicationInfo = &app_info;

#ifdef VK_USE_PLATFORM_WIN32_KHR
    std::vector<const char*> exts = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    ici.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ici.ppEnabledExtensionNames = exts.data();
#endif

    instance_ = vk::createInstance(ici);

    // 2) 물리 디바이스 선택
    auto pdevs = instance_.enumeratePhysicalDevices();
    if (pdevs.empty()) {
        throw std::runtime_error("No Vulkan physical device found");
    }
    physical_device_ = pdevs[0];

    // 3) 그래픽스 큐 인덱스
    auto qprops = physical_device_.getQueueFamilyProperties();
    for (uint32_t i = 0; i < qprops.size(); ++i) {
        if (qprops[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            graphics_queue_index_ = i;
            break;
        }
    }

    // 4) Logical device
    float qPriority = 1.0f;
    vk::DeviceQueueCreateInfo dqci;
    dqci.queueFamilyIndex = graphics_queue_index_;
    dqci.queueCount = 1;
    dqci.pQueuePriorities = &qPriority;

    vk::DeviceCreateInfo dci;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &dqci;
    device_ = physical_device_.createDevice(dci);

    graphics_queue_ = device_.getQueue(graphics_queue_index_, 0);

    // 5) Command Pool
    vk::CommandPoolCreateInfo cpci;
    cpci.queueFamilyIndex = graphics_queue_index_;
    cpci.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    command_pool_ = device_.createCommandPool(cpci);

    // 6) VMA Init
    InitVMA();

    // 7) 동기화
    {
        vk::SemaphoreCreateInfo sci;
        image_available_ = device_.createSemaphore(sci);
        render_finished_ = device_.createSemaphore(sci);

        vk::FenceCreateInfo fci;
        fci.flags = vk::FenceCreateFlagBits::eSignaled;
        in_flight_fence_ = device_.createFence(fci);
    }
}

void VulkanRenderer::InitVMA() {
    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.physicalDevice = physical_device_;
    alloc_info.device = device_;
    alloc_info.instance = instance_;
    vmaCreateAllocator(&alloc_info, &allocator_);
}

void VulkanRenderer::CleanupVulkan() {
    if (!initialized_)
        return;
    device_.waitIdle();

    // destroy pipelines
    for (size_t i = 1; i < pipelines_.size(); ++i) {
        if (pipelines_[i].pipeline) {
            device_.destroyPipeline(pipelines_[i].pipeline);
        }
        if (pipelines_[i].pipeline_layout) {
            device_.destroyPipelineLayout(pipelines_[i].pipeline_layout);
        }
    }
    // buffers
    for (size_t i = 1; i < buffers_.size(); ++i) {
        if (buffers_[i].buffer) {
            vmaDestroyBuffer(allocator_, (VkBuffer)buffers_[i].buffer, buffers_[i].allocation);
        }
    }
    // textures
    for (size_t i = 1; i < textures_.size(); ++i) {
        if (textures_[i].image_view) {
            device_.destroyImageView(textures_[i].image_view);
        }
        if (textures_[i].image) {
            vmaDestroyImage(allocator_, (VkImage)textures_[i].image, textures_[i].allocation);
        }
    }
    // samplers
    for (size_t i = 1; i < samplers_.size(); ++i) {
        if (samplers_[i].sampler) {
            device_.destroySampler(samplers_[i].sampler);
        }
    }
    // shaders
    for (auto& kv : shaders_) {
        if (kv.second.shader_module) {
            device_.destroyShaderModule(kv.second.shader_module);
        }
    }
    shaders_.clear();

    // swapchains
    for (size_t i = 1; i < swapchains_.size(); ++i) {
        auto& sc = swapchains_[i];
        for (auto fb : sc.framebuffers) {
            device_.destroyFramebuffer(fb);
        }
        if (sc.render_pass) {
            device_.destroyRenderPass(sc.render_pass);
        }
        for (auto iv : sc.image_views) {
            device_.destroyImageView(iv);
        }
        if (sc.swapchain) {
            device_.destroySwapchainKHR(sc.swapchain);
        }
    }

    // sync
    if (in_flight_fence_) {
        device_.destroyFence(in_flight_fence_);
    }
    if (image_available_) {
        device_.destroySemaphore(image_available_);
    }
    if (render_finished_) {
        device_.destroySemaphore(render_finished_);
    }

    // command pool
    if (command_pool_) {
        device_.destroyCommandPool(command_pool_);
    }

    // vma
    if (allocator_) {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }

    // device
    if (device_) {
        device_.destroy();
    }
    // instance
    if (instance_) {
        instance_.destroy();
    }
    initialized_ = false;
}

SwapChainHandle VulkanRenderer::CreateSwapChain(const SwapChainDesc& desc) {
    if (!initialized_) {
        InitVulkan();
    }
    return CreateSwapChainInternal(desc);
}

SwapChainHandle VulkanRenderer::CreateSwapChainInternal(const SwapChainDesc& desc) {
#ifdef VK_USE_PLATFORM_WIN32_KHR
    vk::Win32SurfaceCreateInfoKHR sci;
    sci.hinstance = GetModuleHandle(nullptr);
    sci.hwnd = static_cast<HWND>(desc.window_handle);
    vk::SurfaceKHR surface = instance_.createWin32SurfaceKHR(sci);

    VulkanSwapChain sc;
    sc.extent.width = desc.width;
    sc.extent.height = desc.height;

    vk::SwapchainCreateInfoKHR ci;
    ci.surface = surface;
    ci.minImageCount = 2;
    ci.imageFormat = vk::Format::eB8G8R8A8Unorm;
    ci.imageExtent = sc.extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    ci.presentMode = desc.vsync ? vk::PresentModeKHR::eFifo : vk::PresentModeKHR::eImmediate;
    ci.clipped = true;

    sc.swapchain = device_.createSwapchainKHR(ci);
    sc.images = device_.getSwapchainImagesKHR(sc.swapchain);
    sc.image_views.resize(sc.images.size());
    for (size_t i = 0; i < sc.images.size(); ++i) {
        vk::ImageViewCreateInfo ivci;
        ivci.image = sc.images[i];
        ivci.viewType = vk::ImageViewType::e2D;
        ivci.format = ci.imageFormat;
        ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;

        sc.image_views[i] = device_.createImageView(ivci);
    }

    // RenderPass
    {
        vk::AttachmentDescription color_attach;
        color_attach.format = ci.imageFormat;
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

        sc.render_pass = device_.createRenderPass(rpci);
    }
    // Framebuffer
    sc.framebuffers.resize(sc.images.size());
    for (size_t i = 0; i < sc.images.size(); ++i) {
        vk::FramebufferCreateInfo fci;
        fci.renderPass = sc.render_pass;
        vk::ImageView attachments[] = {sc.image_views[i]};
        fci.attachmentCount = 1;
        fci.pAttachments = attachments;
        fci.width = sc.extent.width;
        fci.height = sc.extent.height;
        fci.layers = 1;
        sc.framebuffers[i] = device_.createFramebuffer(fci);
    }

    SwapChainHandle handle = next_swapchain_handle_++;
    if (handle >= swapchains_.size()) {
        swapchains_.resize(handle + 1);
    }
    swapchains_[handle] = sc;

    // Command buffer (이미 1개만 쓸 것이라 가정)
    vk::CommandBufferAllocateInfo cbai;
    cbai.commandPool = command_pool_;
    cbai.level = vk::CommandBufferLevel::ePrimary;
    cbai.commandBufferCount = 1;
    command_buffers_ = device_.allocateCommandBuffers(cbai);

    return handle;
#else
    throw std::runtime_error("Not on Win32 platform");
#endif
}

// 버퍼
BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc& desc) {
    if (!initialized_) {
        InitVulkan();
    }

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
    vb.size_in_bytes = desc.size_in_bytes;

    // VMA 할당
    VmaAllocationCreateInfo aci = {};
    // 예: GPU_ONLY
    aci.usage = VMA_MEMORY_USAGE_AUTO;  // AUTO는 GPU_ONLY 선호
    // 필요 시 CPU_TO_GPU 매핑 가능
    VkBuffer raw_buf;
    VmaAllocation alloc;
    auto result = vmaCreateBuffer(allocator_, (VkBufferCreateInfo*)&bci, &aci, &raw_buf, &alloc, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer with VMA");
    }
    vb.buffer = raw_buf;
    vb.allocation = alloc;

    BufferHandle handle = next_buffer_handle_++;
    if (handle >= buffers_.size()) {
        buffers_.resize(handle + 1);
    }
    buffers_[handle] = vb;
    return handle;
}

void VulkanRenderer::UploadDataToBuffer(const void* data, size_t size, vk::Buffer dst_buffer) {
    // 1) staging buffer
    vk::BufferCreateInfo staging_info;
    staging_info.size = size;
    staging_info.usage = vk::BufferUsageFlagBits::eTransferSrc;

    VmaAllocationCreateInfo aci = {};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;  // CPU visible
    VkBuffer staging_buf;
    VmaAllocation staging_alloc;
    vmaCreateBuffer(allocator_, (VkBufferCreateInfo*)&staging_info, &aci, &staging_buf, &staging_alloc, nullptr);

    // 2) map & memcpy
    void* mapped = nullptr;
    vmaMapMemory(allocator_, staging_alloc, &mapped);
    std::memcpy(mapped, data, size);
    vmaUnmapMemory(allocator_, staging_alloc);

    // 3) 커맨드 버퍼로 copy
    auto cmd = command_buffers_[0];
    cmd.reset();
    vk::CommandBufferBeginInfo begin_info;
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(begin_info);

    vk::BufferCopy bc(0, 0, size);
    cmd.copyBuffer(staging_buf, dst_buffer, bc);

    cmd.end();

    // 4) submit & wait
    vk::SubmitInfo si;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    graphics_queue_.submit(si);
    graphics_queue_.waitIdle();

    // 5) 스테이징 버퍼 해제
    device_.freeCommandBuffers(command_pool_, cmd);
    vmaDestroyBuffer(allocator_, staging_buf, staging_alloc);

    // 재할당 command buffer (1개)
    vk::CommandBufferAllocateInfo cbai;
    cbai.commandPool = command_pool_;
    cbai.level = vk::CommandBufferLevel::ePrimary;
    cbai.commandBufferCount = 1;
    auto cbs = device_.allocateCommandBuffers(cbai);
    command_buffers_ = cbs;
}

void VulkanRenderer::UpdateBuffer(BufferHandle handle, const void* data, size_t size) {
    if (handle == 0 || handle >= buffers_.size()) {
        throw std::runtime_error("Invalid buffer handle");
    }
    auto& vb = buffers_[handle];
    // GPU_ONLY -> staging copy
    UploadDataToBuffer(data, size, vb.buffer);
}

void VulkanRenderer::BindBuffer(BufferHandle handle, uint32_t bind_point) {
    // RecordCommandBuffer()에서 실제 vkCmdBindVertexBuffers/IndexBuffer
}

// 텍스처
TextureHandle VulkanRenderer::CreateTexture(const TextureDesc& desc, const void* initial_data) {
    // image create
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
    vt.width = desc.width;
    vt.height = desc.height;

    // VMA alloc
    VmaAllocationCreateInfo aci = {};
    aci.usage = VMA_MEMORY_USAGE_AUTO;  // GPU_ONLY

    VkImage raw_img;
    VmaAllocation alloc;
    auto result = vmaCreateImage(allocator_, (VkImageCreateInfo*)&ici, &aci, &raw_img, &alloc, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image with VMA");
    }
    vt.image = raw_img;
    vt.allocation = alloc;

    // ImageView
    vk::ImageViewCreateInfo ivci;
    ivci.image = vt.image;
    ivci.viewType = vk::ImageViewType::e2D;
    ivci.format = ici.format;
    ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.layerCount = 1;

    vt.image_view = device_.createImageView(ivci);

    // initial_data -> staging copy + layout transition (생략)

    TextureHandle handle = next_texture_handle_++;
    if (handle >= textures_.size()) {
        textures_.resize(handle + 1);
    }
    textures_[handle] = vt;
    return handle;
}

void VulkanRenderer::BindTexture(TextureHandle handle, uint32_t bind_point) {
    // descriptor set update
}

// 샘플러
SamplerHandle VulkanRenderer::CreateSampler(const SamplerDesc& desc) {
    vk::SamplerCreateInfo sci;
    sci.magFilter = vk::Filter::eLinear;
    sci.minFilter = vk::Filter::eLinear;

    VulkanSampler vs;
    vs.sampler = device_.createSampler(sci);

    SamplerHandle handle = next_sampler_handle_++;
    if (handle >= samplers_.size()) {
        samplers_.resize(handle + 1);
    }
    samplers_[handle] = vs;
    return handle;
}

void VulkanRenderer::BindSampler(SamplerHandle handle, uint32_t bind_point) {
    // descriptor set update
}

// 셰이더
ShaderHandle VulkanRenderer::CreateShader(const ShaderDesc& desc) {
    auto code = ReadFile(desc.file_path);
    auto sm = CreateShaderModule(code);

    VulkanShader vs;
    vs.shader_module = sm;

    ShaderHandle handle = next_shader_handle_++;
    shaders_[handle] = vs;
    return handle;
}

void VulkanRenderer::ReleaseShader(ShaderHandle handle) {
    auto it = shaders_.find(handle);
    if (it != shaders_.end()) {
        device_.destroyShaderModule(it->second.shader_module);
        shaders_.erase(it);
    }
}

vk::ShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char>& code) {
    vk::ShaderModuleCreateInfo smci;
    smci.codeSize = code.size();
    smci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    return device_.createShaderModule(smci);
}

std::vector<char> VulkanRenderer::ReadFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    size_t file_size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(file_size);
    file.seekg(0);
    file.read(buffer.data(), file_size);
    file.close();
    return buffer;
}

// 파이프라인
PipelineHandle VulkanRenderer::CreatePipeline(const PipelineDesc& desc) {
    auto vsh_it = shaders_.find(desc.vertex_shader);
    auto fsh_it = shaders_.find(desc.fragment_shader);
    if (vsh_it == shaders_.end() || fsh_it == shaders_.end()) {
        throw std::runtime_error("Invalid shader handle in pipeline desc");
    }

    vk::PipelineShaderStageCreateInfo vert_stage;
    vert_stage.stage = vk::ShaderStageFlagBits::eVertex;
    vert_stage.module = vsh_it->second.shader_module;
    vert_stage.pName = "main";

    vk::PipelineShaderStageCreateInfo frag_stage;
    frag_stage.stage = vk::ShaderStageFlagBits::eFragment;
    frag_stage.module = fsh_it->second.shader_module;
    frag_stage.pName = "main";

    vk::PipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

    vk::PipelineVertexInputStateCreateInfo vi;
    vk::PipelineInputAssemblyStateCreateInfo ia;
    ia.topology = vk::PrimitiveTopology::eTriangleList;

    vk::Viewport viewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.f, 1.f);
    vk::Rect2D scissor({0, 0}, vk::Extent2D{1280, 720});
    vk::PipelineViewportStateCreateInfo vp;
    vp.viewportCount = 1;
    vp.pViewports = &viewport;
    vp.scissorCount = 1;
    vp.pScissors = &scissor;

    vk::PipelineRasterizationStateCreateInfo rs;
    rs.polygonMode = vk::PolygonMode::eFill;
    rs.cullMode = vk::CullModeFlagBits::eBack;
    rs.frontFace = vk::FrontFace::eCounterClockwise;

    vk::PipelineMultisampleStateCreateInfo ms;
    ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState cbAttach;
    cbAttach.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    cbAttach.blendEnable = false;

    vk::PipelineColorBlendStateCreateInfo cb;
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAttach;

    vk::PipelineLayoutCreateInfo plci;
    auto pipeline_layout = device_.createPipelineLayout(plci);

    if (swapchains_.size() <= 1) {
        throw std::runtime_error("No valid swapchain to create pipeline");
    }
    auto rp = swapchains_[1].render_pass;

    vk::GraphicsPipelineCreateInfo gpci;
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cb;
    gpci.layout = pipeline_layout;
    gpci.renderPass = rp;

    auto res = device_.createGraphicsPipeline(nullptr, gpci);
    if (res.result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to create pipeline");
    }

    VulkanPipeline vpip;
    vpip.pipeline = res.value;
    vpip.pipeline_layout = pipeline_layout;

    PipelineHandle ph = next_pipeline_handle_++;
    if (ph >= pipelines_.size()) {
        pipelines_.resize(ph + 1);
    }
    pipelines_[ph] = vpip;
    return ph;
}

void VulkanRenderer::BindPipeline(PipelineHandle handle) {
    // RecordCommandBuffer()에서 실제 bind
}

// 리소스 해제
void VulkanRenderer::ReleaseResource(uint64_t handle) {
    // swapchain
    if (handle < swapchains_.size() && swapchains_[handle].swapchain) {
        auto& sc = swapchains_[handle];
        for (auto fb : sc.framebuffers) {
            device_.destroyFramebuffer(fb);
        }
        if (sc.render_pass) {
            device_.destroyRenderPass(sc.render_pass);
        }
        for (auto iv : sc.image_views) {
            device_.destroyImageView(iv);
        }
        device_.destroySwapchainKHR(sc.swapchain);
        sc.swapchain = nullptr;
        return;
    }
    // buffer
    if (handle < buffers_.size() && buffers_[handle].buffer) {
        vmaDestroyBuffer(allocator_, (VkBuffer)buffers_[handle].buffer, buffers_[handle].allocation);
        buffers_[handle].buffer = nullptr;
        return;
    }
    // texture
    if (handle < textures_.size() && textures_[handle].image) {
        device_.destroyImageView(textures_[handle].image_view);
        vmaDestroyImage(allocator_, (VkImage)textures_[handle].image, textures_[handle].allocation);
        textures_[handle].image = nullptr;
        return;
    }
    // sampler
    if (handle < samplers_.size() && samplers_[handle].sampler) {
        device_.destroySampler(samplers_[handle].sampler);
        samplers_[handle].sampler = nullptr;
        return;
    }
    // pipeline
    if (handle < pipelines_.size() && pipelines_[handle].pipeline) {
        device_.destroyPipeline(pipelines_[handle].pipeline);
        device_.destroyPipelineLayout(pipelines_[handle].pipeline_layout);
        pipelines_[handle].pipeline = nullptr;
        return;
    }
    // shader
    auto it = shaders_.find(handle);
    if (it != shaders_.end()) {
        device_.destroyShaderModule(it->second.shader_module);
        shaders_.erase(it);
        return;
    }
}

void VulkanRenderer::BeginFrame() {
    device_.waitForFences(in_flight_fence_, VK_TRUE, UINT64_MAX);
    device_.resetFences(in_flight_fence_);
    // acquire image_index=0 (단순화)
}

void VulkanRenderer::EndFrame() {
    // submit command_buffers_[0]
    vk::SubmitInfo si;
    vk::Semaphore wait_sems[] = {image_available_};
    vk::PipelineStageFlags wait_stages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = wait_sems;
    si.pWaitDstStageMask = wait_stages;

    si.commandBufferCount = 1;
    si.pCommandBuffers = &command_buffers_[0];

    vk::Semaphore signal_sems[] = {render_finished_};
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = signal_sems;

    graphics_queue_.submit(si, in_flight_fence_);

    // present (dummy)
}

void VulkanRenderer::RecordCommandBuffer(vk::CommandBuffer cmd, uint32_t image_index) {
    // 실제로는 삼각형 그리기 위해 vkCmdBindPipeline, vkCmdBindVertexBuffers, IndexBuffer, Draw
    // (생략)
}

}  // namespace Lumora
