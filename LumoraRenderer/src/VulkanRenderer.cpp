#include "VulkanRenderer.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#include <vulkan/vulkan_win32.h>  // for vkCreateWin32SurfaceKHR
#endif

namespace Lumora {

// 스태틱 함수
std::unique_ptr<IRenderer> IRenderer::Create() { return std::make_unique<VulkanRenderer>(); }

VulkanRenderer::VulkanRenderer() {
    // lazy init
}

VulkanRenderer::~VulkanRenderer() { CleanupVulkan(); }

// Init / Cleanup
void VulkanRenderer::InitVulkan() {
    if (initialized_) {
        return;
    }
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

    // Win32 extension
    std::vector<const char*> exts = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    ici.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ici.ppEnabledExtensionNames = exts.data();

    instance_ = vk::createInstance(ici);

    // 2) 물리 디바이스
    auto pdevs = instance_.enumeratePhysicalDevices();
    if (pdevs.empty()) {
        throw std::runtime_error("No physical device found!");
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

    // 4) Logical Device
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

    // 5) 커맨드 풀 등
    vk::CommandPoolCreateInfo cpci;
    cpci.queueFamilyIndex = graphics_queue_index_;
    cpci.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    command_pool_ = device_.createCommandPool(cpci);

    // ...동기화 객체 등
}

// Cleanup
void VulkanRenderer::CleanupVulkan() {
    if (!initialized_) {
        return;
    }
    device_.waitIdle();

    // 스왑체인
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

    // 파이프라인
    for (size_t i = 1; i < pipelines_.size(); ++i) {
        auto& p = pipelines_[i];
        if (p.pipeline) {
            device_.destroyPipeline(p.pipeline);
        }
        if (p.pipeline_layout) {
            device_.destroyPipelineLayout(p.pipeline_layout);
        }
    }
    // 버퍼
    for (size_t i = 1; i < buffers_.size(); ++i) {
        if (buffers_[i].buffer) {
            device_.destroyBuffer(buffers_[i].buffer);
        }
    }
    // 텍스처
    for (size_t i = 1; i < textures_.size(); ++i) {
        if (textures_[i].image_view) {
            device_.destroyImageView(textures_[i].image_view);
        }
        if (textures_[i].image) {
            device_.destroyImage(textures_[i].image);
        }
    }
    // 샘플러
    for (size_t i = 1; i < samplers_.size(); ++i) {
        if (samplers_[i].sampler) {
            device_.destroySampler(samplers_[i].sampler);
        }
    }
    // 셰이더
    for (auto& kv : shaders_) {
        if (kv.second.shader_module) {
            device_.destroyShaderModule(kv.second.shader_module);
        }
    }
    shaders_.clear();

    // etc
    if (command_pool_) {
        device_.destroyCommandPool(command_pool_);
    }
    if (device_) {
        device_.destroy();
    }
    if (instance_) {
        instance_.destroy();
    }

    initialized_ = false;
}

// SwapChain
SwapChainHandle VulkanRenderer::CreateSwapChain(const SwapChainDesc& desc) {
    if (!initialized_) {
        InitVulkan();
    }
    return CreateSwapChainInternal(desc);
}

SwapChainHandle VulkanRenderer::CreateSwapChainInternal(const SwapChainDesc& desc) {
    // 1) Win32 surface
    // 실제로 HWND=desc.window_handle -> vkCreateWin32SurfaceKHR
    // 예시:
    vk::Win32SurfaceCreateInfoKHR sci;
    sci.hinstance = GetModuleHandle(nullptr);
    sci.hwnd = static_cast<HWND>(desc.window_handle);
    vk::SurfaceKHR surface = instance_.createWin32SurfaceKHR(sci);

    // 2) Swapchain
    VulkanSwapChain sc;
    sc.extent.width = desc.width;
    sc.extent.height = desc.height;

    vk::SwapchainCreateInfoKHR ci;
    ci.minImageCount = 2;
    ci.imageFormat = vk::Format::eB8G8R8A8Unorm;
    ci.imageExtent = sc.extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    ci.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    ci.presentMode = desc.vsync ? vk::PresentModeKHR::eFifo : vk::PresentModeKHR::eImmediate;
    ci.surface = surface;
    // ...등등
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

    // 3) RenderPass
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

    // 4) Framebuffer
    sc.framebuffers.resize(sc.images.size());
    for (size_t i = 0; i < sc.images.size(); ++i) {
        vk::ImageView attachments[] = {sc.image_views[i]};
        vk::FramebufferCreateInfo fci;
        fci.renderPass = sc.render_pass;
        fci.attachmentCount = 1;
        fci.pAttachments = attachments;
        fci.width = sc.extent.width;
        fci.height = sc.extent.height;
        fci.layers = 1;
        sc.framebuffers[i] = device_.createFramebuffer(fci);
    }

    // 핸들 등록
    SwapChainHandle handle = next_swapchain_handle_++;
    if (handle >= swapchains_.size()) {
        swapchains_.resize(handle + 1);
    }
    swapchains_[handle] = sc;
    return handle;
}

// Buffer
BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc& desc) {
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

    BufferHandle handle = next_buffer_handle_++;
    if (handle >= buffers_.size()) {
        buffers_.resize(handle + 1);
    }
    buffers_[handle] = vb;
    return handle;
}

void VulkanRenderer::UpdateBuffer(BufferHandle handle, const void* data, size_t size) {
    // VMA or staging etc.
}

void VulkanRenderer::BindBuffer(BufferHandle handle, uint32_t bind_point) {
    // e.g. vkCmdBindVertexBuffers, vkCmdBindIndexBuffer
}

// Texture
TextureHandle VulkanRenderer::CreateTexture(const TextureDesc& desc, const void* initial_data) {
    // 2D or 3D or CUBE
    vk::ImageCreateInfo ici;
    if (desc.is_3d) {
        ici.imageType = vk::ImageType::e3D;
    } else {
        ici.imageType = vk::ImageType::e2D;
    }
    ici.extent.width = desc.width;
    ici.extent.height = desc.height;
    ici.extent.depth = desc.is_3d ? 8 : 1;  // 예시
    ici.mipLevels = 1;
    ici.arrayLayers = desc.is_cube_map ? 6 : 1;  // 큐브맵 예시
    ici.format = vk::Format::eR8G8B8A8Unorm;
    ici.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

    VulkanTexture vt;
    vt.image = device_.createImage(ici);
    vt.width = desc.width;
    vt.height = desc.height;

    // ImageView
    vk::ImageViewCreateInfo ivci;
    ivci.image = vt.image;
    ivci.viewType =
        (desc.is_cube_map) ? vk::ImageViewType::eCube : (desc.is_3d ? vk::ImageViewType::e3D : vk::ImageViewType::e2D);
    ivci.format = ici.format;
    ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.layerCount = (desc.is_cube_map ? 6 : 1);

    vt.image_view = device_.createImageView(ivci);

    TextureHandle handle = next_texture_handle_++;
    if (handle >= textures_.size()) {
        textures_.resize(handle + 1);
    }
    textures_[handle] = vt;
    return handle;
}

void VulkanRenderer::BindTexture(TextureHandle handle, uint32_t bind_point) {
    // Descriptor set update
}

// Sampler
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

// Shader
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

std::vector<char> VulkanRenderer::ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    size_t fsize = (size_t)file.tellg();
    std::vector<char> data(fsize);
    file.seekg(0);
    file.read(data.data(), fsize);
    file.close();
    return data;
}

// Pipeline
PipelineHandle VulkanRenderer::CreatePipeline(const PipelineDesc& desc) {
    // ...
    PipelineHandle ph = next_pipeline_handle_++;
    // create pipeline & pipeline_layout
    // store in pipelines_[ph]
    return ph;
}

void VulkanRenderer::BindPipeline(PipelineHandle handle) {
    // cmdBindPipeline
}

// ReleaseResource
void VulkanRenderer::ReleaseResource(uint64_t handle) {
    // check swapchains
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
        if (sc.swapchain) {
            device_.destroySwapchainKHR(sc.swapchain);
        }
        sc.swapchain = nullptr;
        return;
    }
    // check buffers
    if (handle < buffers_.size() && buffers_[handle].buffer) {
        device_.destroyBuffer(buffers_[handle].buffer);
        buffers_[handle].buffer = nullptr;
        return;
    }
    // check textures
    if (handle < textures_.size() && textures_[handle].image) {
        device_.destroyImageView(textures_[handle].image_view);
        device_.destroyImage(textures_[handle].image);
        textures_[handle].image = nullptr;
        return;
    }
    // check samplers
    if (handle < samplers_.size() && samplers_[handle].sampler) {
        device_.destroySampler(samplers_[handle].sampler);
        samplers_[handle].sampler = nullptr;
        return;
    }
    // check pipelines
    if (handle < pipelines_.size() && pipelines_[handle].pipeline) {
        device_.destroyPipeline(pipelines_[handle].pipeline);
        device_.destroyPipelineLayout(pipelines_[handle].pipeline_layout);
        pipelines_[handle].pipeline = nullptr;
        return;
    }
    // check shaders
    auto it = shaders_.find(handle);
    if (it != shaders_.end()) {
        device_.destroyShaderModule(it->second.shader_module);
        shaders_.erase(it);
        return;
    }
}

// Frame
void VulkanRenderer::BeginFrame() {
    // acquire swapchain image, reset fence, record cmd
}

void VulkanRenderer::EndFrame() {
    // present
}

}  // namespace Lumora
