#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "../include/IRenderer.h"
#include "VulkanRenderer.h"

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

    // 디스크립터 풀, 레이아웃 생성 (UBO + CombinedSampler)
    {
        // 디스크립터 풀
        std::vector<vk::DescriptorPoolSize> pool_sizes = {
            {vk::DescriptorType::eUniformBuffer, 100},
            {vk::DescriptorType::eCombinedImageSampler, 100},
        };
        vk::DescriptorPoolCreateInfo dpci;
        dpci.poolSizeCount = (uint32_t)pool_sizes.size();
        dpci.pPoolSizes = pool_sizes.data();
        dpci.maxSets = 100;
        descriptor_pool_ = device_.createDescriptorPool(dpci);
        // 레이아웃 binding=0 -> UBO, binding=1 -> sampler2D
        vk::DescriptorSetLayoutBinding ubo_bind;
        ubo_bind.binding = 0;
        ubo_bind.descriptorType = vk::DescriptorType::eUniformBuffer;
        ubo_bind.descriptorCount = 1;
        ubo_bind.stageFlags = vk::ShaderStageFlagBits::eVertex;
        vk::DescriptorSetLayoutBinding samp_bind;
        samp_bind.binding = 1;
        samp_bind.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        samp_bind.descriptorCount = 1;
        samp_bind.stageFlags = vk::ShaderStageFlagBits::eFragment;
        std::vector<vk::DescriptorSetLayoutBinding> bindings = {ubo_bind, samp_bind};
        vk::DescriptorSetLayoutCreateInfo dsci;
        dsci.bindingCount = (uint32_t)bindings.size();
        dsci.pBindings = bindings.data();
        descriptor_set_layout_ = device_.createDescriptorSetLayout(dsci);
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

    // create depth image (eg: D32_SFLOAT)
    {
        vk::ImageCreateInfo ici;
        ici.imageType = vk::ImageType::e2D;
        ici.extent.width = sc.extent.width;
        ici.extent.height = sc.extent.height;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.format = vk::Format::eD32Sfloat;
        ici.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;

        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VkImage raw_img;
        VmaAllocation alloc;
        auto rr = vmaCreateImage(allocator_, (VkImageCreateInfo*)&ici, &aci, &raw_img, &alloc, nullptr);
        if (rr != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image");
        }
        sc.depth_image = raw_img;
        sc.depth_alloc = alloc;

        // depth view
        vk::ImageViewCreateInfo ivci;
        ivci.image = sc.depth_image;
        ivci.viewType = vk::ImageViewType::e2D;
        ivci.format = vk::Format::eD32Sfloat;
        ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        sc.depth_view = device_.createImageView(ivci);
    }

    m_swapchainImageCount = (uint32_t)sc.images.size();
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

    // renderpass with depth
    {
        // color attach
        vk::AttachmentDescription color_attach;
        color_attach.format = ci.imageFormat;
        color_attach.samples = vk::SampleCountFlagBits::e1;
        color_attach.loadOp = vk::AttachmentLoadOp::eClear;
        color_attach.storeOp = vk::AttachmentStoreOp::eStore;
        color_attach.initialLayout = vk::ImageLayout::eUndefined;
        color_attach.finalLayout = vk::ImageLayout::ePresentSrcKHR;

        // depth attach
        vk::AttachmentDescription depth_attach;
        depth_attach.format = vk::Format::eD32Sfloat;
        depth_attach.samples = vk::SampleCountFlagBits::e1;
        depth_attach.loadOp = vk::AttachmentLoadOp::eClear;
        depth_attach.storeOp = vk::AttachmentStoreOp::eDontCare;
        depth_attach.initialLayout = vk::ImageLayout::eUndefined;
        depth_attach.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        std::vector<vk::AttachmentDescription> attaches = {color_attach, depth_attach};

        vk::AttachmentReference color_ref;
        color_ref.attachment = 0;
        color_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentReference depth_ref;
        depth_ref.attachment = 1;
        depth_ref.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass;
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;

        vk::RenderPassCreateInfo rpci;
        rpci.attachmentCount = (uint32_t)attaches.size();
        rpci.pAttachments = attaches.data();
        rpci.subpassCount = 1;
        rpci.pSubpasses = &subpass;
        sc.render_pass = device_.createRenderPass(rpci);
    }

    // Framebuffer
    sc.framebuffers.resize(sc.images.size());
    for (size_t i = 0; i < sc.images.size(); ++i) {
        vk::FramebufferCreateInfo fci;
        fci.renderPass = sc.render_pass;
        vk::ImageView attachments[] = {sc.image_views[i], sc.depth_view};
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

    // Command Buffers = swapchainImageCount
    {
        vk::CommandBufferAllocateInfo cbai;
        cbai.commandPool = command_pool_;
        cbai.level = vk::CommandBufferLevel::ePrimary;
        cbai.commandBufferCount = m_swapchainImageCount;
        m_commandBuffers = device_.allocateCommandBuffers(cbai);
    }

    // Sync objects = kMaxFramesInFlight
    m_imageAvailable.resize(kMaxFramesInFlight);
    m_renderFinished.resize(kMaxFramesInFlight);
    m_inFlightFences.resize(kMaxFramesInFlight);
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        m_imageAvailable[i] = device_.createSemaphore({});
        m_renderFinished[i] = device_.createSemaphore({});
        vk::FenceCreateInfo fci;
        fci.flags = vk::FenceCreateFlagBits::eSignaled;
        m_inFlightFences[i] = device_.createFence(fci);
    }

    // descriptor sets도 swapchainImageCount만큼 allocate (예: m_descriptorSets)
    // (descriptor_set_layout_ 이미 존재한다고 가정)
    {
        std::vector<vk::DescriptorSetLayout> layouts(m_swapchainImageCount, descriptor_set_layout_);
        vk::DescriptorSetAllocateInfo dsai;
        dsai.descriptorPool = descriptor_pool_;
        dsai.descriptorSetCount = m_swapchainImageCount;
        dsai.pSetLayouts = layouts.data();
        m_descriptorSets = device_.allocateDescriptorSets(dsai);
        // 이후 BindXXX 시, imageIndex에 따라 m_descriptorSets[imageIndex] 업데이트
    }

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

    // 메모리 사용 정책
    VmaAllocationCreateInfo vmaAllocCI = {};
    switch (desc.memory_usage) {
        case BufferDesc::MemoryUsage::GpuOnly:
            vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case BufferDesc::MemoryUsage::CpuToGpu:
            vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            break;
        default:
            vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
            break;
    }

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
    if (handle == 0 || handle >= buffers_.size()) {
        throw std::runtime_error("Invalid buffer handle in BindBuffer");
    }
    const auto& vb = buffers_[handle];

    // 2-1) 버텍스/인덱스 버퍼라면, RecordCommandBuffer에서 참고하도록
    //       임시로 보관 (예: m_currentVertexBufferHandle 등)
    // 2-2) 유니폼 버퍼라면 -> descriptor set binding 업데이트

    // 사용 예시: bind_point = 0 -> vertex, 1 -> index, 10 -> ubo binding(=10)
    // (실제로는 더 체계적인 매핑 필요)

    if (vb.buffer) {
        // 예시 분기:
        if (bind_point == 0) {
            // vertex buffer
            // 임시 보관
            m_boundVertexBufferHandle_ = handle;
        } else if (bind_point == 1) {
            // index buffer
            m_boundIndexBufferHandle_ = handle;
        } else {
            uint32_t imageIndex = m_currentSwapchainImageIndex;
            auto ds = m_descriptorSets[imageIndex];

            // 유니폼 버퍼 (descriptor set 업데이트)
            vk::DescriptorBufferInfo dbi;
            dbi.buffer = vb.buffer;
            dbi.offset = 0;
            dbi.range = vb.size_in_bytes;  // 전체

            // descriptor_sets_[0]의 binding=bind_point에 연결한다고 가정
            vk::WriteDescriptorSet wds;
            wds.dstSet = ds;
            wds.dstBinding = bind_point;
            wds.descriptorCount = 1;
            wds.descriptorType = vk::DescriptorType::eUniformBuffer;
            wds.pBufferInfo = &dbi;

            device_.updateDescriptorSets(wds, {});
        }
    }
}

// 텍스처
TextureHandle VulkanRenderer::CreateTexture(const TextureDesc& desc, const void* initial_data) {
    // image create
    vk::ImageCreateInfo ici;
    ici.imageType = vk::ImageType::e2D;
    ici.extent.width = desc.width;
    ici.extent.height = desc.height;
    ici.extent.depth = 1;
    ici.mipLevels = desc.mip_levels;
    ici.arrayLayers = desc.array_layers;

    // format 매핑
    vk::Format vkformat = vk::Format::eR8G8B8A8Unorm;
    switch (desc.format) {
        case TextureDesc::Format::RGBA8_SRGB:
            vkformat = vk::Format::eR8G8B8A8Srgb;
            break;
        default:
            vkformat = vk::Format::eR8G8B8A8Unorm;
            break;
    }
    ici.format = vkformat;

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
    if (handle == 0 || handle >= textures_.size()) {
        throw std::runtime_error("Invalid texture handle in BindTexture");
    }
    const auto& tex = textures_[handle];

    // descriptor set(binding=bind_point)에 CombinedImageSampler를 업데이트
    // (Sampler는 BindSampler에서 할 수도 있지만, 여기서는 Texture+Sampler 합쳐서 처리 가능)

    // 임시로 sampler는 별도로, 또는 하나로 처리
    // 여기서는 Sampler를 미리 m_boundSamplerHandle_에 저장해둘 수도 있음
    // ...

    vk::DescriptorImageInfo dii;
    dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    dii.imageView = tex.image_view;
    // sampler는 BindSampler()에서 업데이트 or m_boundSamplerHandle_ 참고

    vk::WriteDescriptorSet wds;
    wds.dstSet = descriptor_sets_[0];
    wds.dstBinding = bind_point;
    wds.descriptorCount = 1;
    wds.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    wds.pImageInfo = &dii;

    device_.updateDescriptorSets(wds, {});
}

// 샘플러
SamplerHandle VulkanRenderer::CreateSampler(const SamplerDesc& desc) {
    vk::SamplerCreateInfo sci;
    // filter
    auto toVkFilter = [](SamplerDesc::Filter f) -> vk::Filter {
        return (f == SamplerDesc::Filter::Nearest) ? vk::Filter::eNearest : vk::Filter::eLinear;
    };
    sci.minFilter = toVkFilter(desc.filter_min);
    sci.magFilter = toVkFilter(desc.filter_mag);

    // address mode
    auto toVkAddress = [](SamplerDesc::AddressMode am) -> vk::SamplerAddressMode {
        switch (am) {
            case SamplerDesc::AddressMode::ClampToEdge:
                return vk::SamplerAddressMode::eClampToEdge;
            default:
                return vk::SamplerAddressMode::eRepeat;
        }
    };
    sci.addressModeU = toVkAddress(desc.address_mode_u);
    sci.addressModeV = toVkAddress(desc.address_mode_v);
    sci.addressModeW = toVkAddress(desc.address_mode_w);

    // mip lod
    sci.mipLodBias = desc.mip_lod_bias;
    sci.minLod = desc.min_lod;
    sci.maxLod = desc.max_lod;
    sci.anisotropyEnable = desc.enable_anisotropy;
    sci.maxAnisotropy = desc.max_anisotropy;

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
    if (handle == 0 || handle >= samplers_.size()) {
        throw std::runtime_error("Invalid sampler handle in BindSampler");
    }
    const auto& samp = samplers_[handle];

    // 만약 sampler를 텍스처와 분리해서 binding한다면 descriptorType=eSampler
    // 하지만 보통 combinedImageSampler로 같이 binding. 취향/설계 따라 다름.
    // 예시로 sampler만 업데이트:
    vk::DescriptorImageInfo dii;
    dii.sampler = samp.sampler;
    dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    dii.imageView = VK_NULL_HANDLE;  // 텍스처는 별도

    vk::WriteDescriptorSet wds;
    wds.dstSet = descriptor_sets_[0];
    wds.dstBinding = bind_point;
    wds.descriptorCount = 1;
    wds.descriptorType = vk::DescriptorType::eSampler;
    wds.pImageInfo = &dii;

    device_.updateDescriptorSets(wds, {});
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

    // raster
    vk::PipelineRasterizationStateCreateInfo rs;
    switch (desc.polygon_mode) {
        case PipelineDesc::PolygonMode::Line:
            rs.polygonMode = vk::PolygonMode::eLine;
            break;
        case PipelineDesc::PolygonMode::Point:
            rs.polygonMode = vk::PolygonMode::ePoint;
            break;
        default:
            rs.polygonMode = vk::PolygonMode::eFill;
            break;
    }
    switch (desc.cull_mode) {
        case PipelineDesc::CullMode::None:
            rs.cullMode = vk::CullModeFlagBits::eNone;
            break;
        case PipelineDesc::CullMode::Front:
            rs.cullMode = vk::CullModeFlagBits::eFront;
            break;
        case PipelineDesc::CullMode::Back:
            rs.cullMode = vk::CullModeFlagBits::eBack;
            break;
        default:
            rs.cullMode = vk::CullModeFlagBits::eFrontAndBack;
            break;
    }
    rs.frontFace =
        (desc.front_face == PipelineDesc::FrontFace::CW) ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise;

    // depth/stencil
    vk::PipelineDepthStencilStateCreateInfo ds;
    ds.depthTestEnable = desc.enable_depth_test;
    ds.depthWriteEnable = desc.enable_depth_write;
    ds.depthCompareOp = vk::CompareOp::eLess;
    // stencil 생략

    vk::PipelineMultisampleStateCreateInfo ms;
    ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // blend
    vk::PipelineColorBlendAttachmentState cbAttach;
    cbAttach.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    cbAttach.blendEnable = desc.blend_enable;

    vk::PipelineColorBlendStateCreateInfo cb;
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAttach;

    vk::PipelineLayoutCreateInfo plci;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &descriptor_set_layout_;

    // PushConstantRange 간단 예시: 최대 128바이트, Vertex+Fragment 단계
    vk::PushConstantRange pcr;              // #FIXME PushConstant 내부적으로 자동
    pcr.stageFlags = m_pushConstantStages;  // 예: Vertex|Fragment
    pcr.offset = 0;
    pcr.size = 128;  // 원하는 최대 범위

    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;

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
    gpci.pDepthStencilState = &ds;
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
        if (sc.depth_view) {
            device_.destroyImageView(sc.depth_view);
            sc.depth_view = nullptr;
        }
        if (sc.depth_image) {
            vmaDestroyImage(allocator_, (VkImage)sc.depth_image, sc.depth_alloc);
            sc.depth_image = nullptr;
            sc.depth_alloc = nullptr;
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
    device_.waitForFences(m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    device_.resetFences(m_inFlightFences[m_currentFrame]);

    auto [result, imageIndex] = device_.acquireNextImageKHR(swapchains_[1].swapchain,  // 예: handle=1
                                                            UINT64_MAX, m_imageAvailable[m_currentFrame], nullptr);
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        // handle error
    }
    m_currentSwapchainImageIndex = imageIndex;  // 멤버 추가 가정

    // commandBuffers[imageIndex].reset() if needed
    // Record cmd
}

void VulkanRenderer::EndFrame() {
    // submit commandBuffers[m_currentSwapchainImageIndex]
    vk::SubmitInfo si;
    vk::Semaphore waitSemaphores[] = {m_imageAvailable[m_currentFrame]};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = waitSemaphores;
    si.pWaitDstStageMask = waitStages;

    // cmd
    si.commandBufferCount = 1;
    si.pCommandBuffers = &m_commandBuffers[m_currentSwapchainImageIndex];

    vk::Semaphore signalSemaphores[] = {m_renderFinished[m_currentFrame]};
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = signalSemaphores;

    graphics_queue_.submit(si, m_inFlightFences[m_currentFrame]);

    // present
    vk::PresentInfoKHR pi;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = signalSemaphores;
    vk::SwapchainKHR sc[] = {swapchains_[1].swapchain};
    pi.swapchainCount = 1;
    pi.pSwapchains = sc;
    pi.pImageIndices = &m_currentSwapchainImageIndex;

    graphics_queue_.presentKHR(pi);

    m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
}

void VulkanRenderer::RecordCommandBuffer(vk::CommandBuffer cmd, uint32_t image_index) {
    // 예시: swapchains_[1] 사용
    if (swapchains_.size() <= 1)
        return;
    auto& sc = swapchains_[1];
    cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    vk::RenderPassBeginInfo rpbi;
    rpbi.renderPass = sc.render_pass;
    rpbi.framebuffer = sc.framebuffers[image_index];
    rpbi.renderArea.offset = vk::Offset2D{0, 0};
    rpbi.renderArea.extent = sc.extent;
    vk::ClearValue clear_col = vk::ClearColorValue(std::array<float, 4>{0.2f, 0.3f, 0.4f, 1.f});
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear_col;
    cmd.beginRenderPass(rpbi, vk::SubpassContents::eInline);
    // 파이프라인 바인딩 (가정: pipelines_[1] 존재)
    if (pipelines_.size() > 1 && pipelines_[1].pipeline) {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines_[1].pipeline);
        // descriptor set 바인딩 (가정: descriptor_sets_[0] 유효)
        if (!descriptor_sets_.empty()) {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelines_[1].pipeline_layout, 0,
                                   descriptor_sets_[0], {});
        }
    }
    // 정점 버퍼
    if (m_boundVertexBufferHandle_ < buffers_.size() && buffers_[m_boundVertexBufferHandle_].buffer) {
        vk::Buffer vb = buffers_[m_boundVertexBufferHandle_].buffer;
        vk::DeviceSize off = 0;
        cmd.bindVertexBuffers(0, vb, off);
    }
    // 인덱스 버퍼
    if (m_boundIndexBufferHandle_ < buffers_.size() && buffers_[m_boundIndexBufferHandle_].buffer) {
        cmd.bindIndexBuffer(buffers_[m_boundIndexBufferHandle_].buffer, 0, vk::IndexType::eUint16);
    }
    // drawIndexed
    cmd.drawIndexed(index_count_, 1, 0, 0, 0);

    cmd.endRenderPass();
    cmd.end();
}

// 4) 테스트용: descriptor set 할당/업데이트 (UBO + sampler)
//  (이 로직은 스왑체인 생성 직후나, UBO/텍스처 생성 뒤에 호출)
//  여기서는 예시로 descriptor_sets_.resize(1)하고, UBO/Texture를 연결
//  (실제 코드에서는 좀 더 동적으로 구성)
void VulkanRenderer::CreateTestDescriptorSet(BufferHandle ubo, TextureHandle tex, SamplerHandle samp) {
    // descriptor set alloc
    vk::DescriptorSetAllocateInfo dsai;
    dsai.descriptorPool = descriptor_pool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &descriptor_set_layout_;
    auto sets = device_.allocateDescriptorSets(dsai);
    descriptor_sets_ = sets;  // 하나만
    // UBO binding=0
    vk::DescriptorBufferInfo dbi;
    dbi.buffer = buffers_[ubo].buffer;
    dbi.offset = 0;
    dbi.range = buffers_[ubo].size_in_bytes;  // or sizeof(UBO struct)
    vk::WriteDescriptorSet wds_ubo;
    wds_ubo.dstSet = descriptor_sets_[0];
    wds_ubo.dstBinding = 0;
    wds_ubo.descriptorCount = 1;
    wds_ubo.descriptorType = vk::DescriptorType::eUniformBuffer;
    wds_ubo.pBufferInfo = &dbi;
    // Texture + Sampler binding=1
    vk::DescriptorImageInfo dii;
    dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    dii.imageView = textures_[tex].image_view;
    dii.sampler = samplers_[samp].sampler;
    vk::WriteDescriptorSet wds_tex;
    wds_tex.dstSet = descriptor_sets_[0];
    wds_tex.dstBinding = 1;
    wds_tex.descriptorCount = 1;
    wds_tex.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    wds_tex.pImageInfo = &dii;
    std::vector<vk::WriteDescriptorSet> writes = {wds_ubo, wds_tex};
    device_.updateDescriptorSets(writes, {});
}

// 2) 새로 추가된 PushConstants() 함수 구현
void VulkanRenderer::PushConstants(uint32_t offset, uint32_t size, const void* data) {
    // 커맨드 버퍼에 pushConstants 기록
    // (예: 스왑체인 imageIndex 기반으로 m_commandBuffers[imageIndex])
    if (m_commandBuffers.empty()) {
        return;  // 혹은 throw
    }

    uint32_t imageIndex = m_currentSwapchainImageIndex;  // 가정
    auto cmd = m_commandBuffers[imageIndex];

    // 파이프라인 레이아웃은 pipelines_[활성화된 handle].pipeline_layout 이나
    // 혹은 "현재 파이프라인" 등에서 가져와야 함. 간단히 pipelines_[1]이라 가정:
    if (pipelines_.size() <= 1 || !pipelines_[1].pipeline_layout) {
        return;
    }
    vk::PipelineLayout layout = pipelines_[1].pipeline_layout;

    // 커맨드 버퍼에 push
    cmd.pushConstants(layout,
                      m_pushConstantStages,  // 보통 Vertex|Fragment
                      offset,                // 0
                      size,                  // 사용자 지정
                      data);
}
}  // namespace Lumora
