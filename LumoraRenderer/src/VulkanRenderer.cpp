#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
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

namespace lumora {

// Helper function to generate unique IDs for handles
static uint64_t GenerateUniqueID() {
    static uint64_t current_id = 1;
    return current_id++;
}

class VulkanRenderer : public IRenderer {
   public:
    VulkanRenderer();
    ~VulkanRenderer();

    void Open(const char* app_name) override;
    void Close() override;

    SwapChainHandle CreateSwapChain(const SwapChainDesc& desc) override;
    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void UpdateBuffer(BufferHandle handle, const void* data, size_t size) override;
    void BindBuffer(BufferHandle handle, uint32_t bind_point, uint32_t dynamic_offset = 0) override;
    TextureHandle CreateTexture(const TextureDesc& desc) override;
    void BindTexture(TextureHandle handle, uint32_t bind_point) override;
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void BindSampler(SamplerHandle handle, uint32_t bind_point) override;
    ShaderHandle CreateShader(const ShaderDesc& desc) override;
    PipelineHandle CreatePipeline(const PipelineDesc& desc) override;
    PipelineHandle CreatePipeline(const ComputePipelineDesc& desc) override;
    void BindPipeline(PipelineHandle handle) override;

    void DispatchCompute(uint32_t group_x, uint32_t group_y, uint32_t group_z) override;

    void BeginPass(const RenderPassDesc& desc) override;
    void EndPass() override;
    void Render(const SwapChainHandle& handle, std::function<void()> callback) override;
    void DrawIndexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0,
                     int32_t vertex_offset = 0, uint32_t first_instance = 0) override;
    bool ReloadShader(ShaderHandle handle, const ShaderDesc& new_desc) override;

    void ReleaseResource(SwapChainHandle handle) override;
    void ReleaseResource(TextureHandle handle) override;
    void ReleaseResource(SamplerHandle handle) override;
    void ReleaseResource(PipelineHandle handle) override;
    void ReleaseResource(ShaderHandle handle) override;

   private:
    // Vulkan core components
    vk::Instance instance;
    vk::PhysicalDevice physicalDevice;
    vk::Device device;
    vk::Queue graphicsQueue;
    uint32_t graphicsQueueFamily;
    vk::SurfaceKHR surface;
    vk::SwapchainKHR swapchain;
    std::vector<vk::Image> swapchainImages;
    vk::Format swapchainImageFormat;
    vk::Extent2D swapchainExtent;
    std::vector<vk::ImageView> swapchainImageViews;
    vk::RenderPass renderPass;
    vk::PipelineLayout pipelineLayout;
    vk::CommandPool commandPool;

    // VMA Allocator
    VmaAllocator allocator;

    // Resource maps
    std::unordered_map<uint64_t, vk::Buffer> buffers;
    std::unordered_map<uint64_t, VmaAllocation> bufferAllocations;

    std::unordered_map<uint64_t, vk::Image> images;
    std::unordered_map<uint64_t, VmaAllocation> imageAllocations;
    std::unordered_map<uint64_t, vk::ImageView> imageViews;

    std::unordered_map<uint64_t, vk::Sampler> samplers;

    std::unordered_map<uint64_t, vk::ShaderModule> shaders;

    std::unordered_map<uint64_t, vk::Pipeline> pipelines;

    // Handle to index mapping
    std::mutex resourceMutex;

    // Current command buffer
    vk::CommandBuffer currentCommandBuffer;

    // Frame synchronization
    // (For simplicity, not implemented here)

    // Internal methods
    void InitVulkan(const char* app_name);
    void CleanupVulkan();
    void CreateInstance(const char* app_name);
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSurface(const SwapChainDesc& desc);
    void CreateSwapChainInternal(const SwapChainDesc& desc);
    void CreateImageViews();
    void CreateRenderPass(const RenderPassDesc& desc);
    void CreateCommandPool();
    void AllocateCommandBuffer();

    // Helper methods
    vk::ShaderModule CreateShaderModule(const std::string& code);

    Format FromVulkanFormat(vk::Format vk_format);
    vk::Format ToVulkanFormat(Format format);
};

// Implementation

VulkanRenderer::VulkanRenderer() {
    // Constructor
}

VulkanRenderer::~VulkanRenderer() { Close(); }

void VulkanRenderer::Open(const char* app_name) { InitVulkan(app_name); }

void VulkanRenderer::Close() { CleanupVulkan(); }

void VulkanRenderer::InitVulkan(const char* app_name) {
    // Create Vulkan Instance
    CreateInstance(app_name);

    // Pick Physical Device
    PickPhysicalDevice();

    // Create Logical Device
    CreateLogicalDevice();

    // Initialize VMA
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = static_cast<VkPhysicalDevice>(physicalDevice);
    allocatorInfo.device = static_cast<VkDevice>(device);
    allocatorInfo.instance = static_cast<VkInstance>(instance);
    vmaCreateAllocator(&allocatorInfo, &allocator);
}

void VulkanRenderer::CleanupVulkan() {
    std::lock_guard<std::mutex> lock(resourceMutex);

    // Destroy all pipelines
    for (auto& [id, pipeline] : pipelines) {
        device.destroyPipeline(pipeline);
    }
    pipelines.clear();

    // Destroy all shader modules
    for (auto& [id, shader] : shaders) {
        device.destroyShaderModule(shader);
    }
    shaders.clear();

    // Destroy all samplers
    for (auto& [id, sampler] : samplers) {
        device.destroySampler(sampler);
    }
    samplers.clear();

    // Destroy all image views and images
    for (auto& [id, imageView] : imageViews) {
        device.destroyImageView(imageView);
    }
    imageViews.clear();

    for (auto& [id, image] : images) {
        vmaDestroyImage(allocator, static_cast<VkImage>(image), bufferAllocations[id]);
    }
    images.clear();
    imageAllocations.clear();

    // Destroy all buffers
    for (auto& [id, buffer] : buffers) {
        vmaDestroyBuffer(allocator, static_cast<VkBuffer>(buffer), bufferAllocations[id]);
    }
    buffers.clear();
    bufferAllocations.clear();

    // Destroy swapchain and image views
    if (swapchain) {
        device.destroySwapchainKHR(swapchain);
    }
    for (auto& imageView : swapchainImageViews) {
        device.destroyImageView(imageView);
    }
    swapchainImageViews.clear();

    // Destroy VMA allocator
    if (allocator) {
        vmaDestroyAllocator(allocator);
    }

    // Destroy Vulkan device
    if (device) {
        device.destroy();
    }

    // Destroy Vulkan instance
    if (instance) {
        instance.destroy();
    }
}

void VulkanRenderer::CreateInstance(const char* app_name) {
    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = app_name;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Lumora";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    vk::InstanceCreateInfo createInfo;
    createInfo.pApplicationInfo = &appInfo;

    // Enable necessary extensions
    std::vector<const char*> extensions = {VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
                                           VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#elif defined(__linux__)
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME // 또는 VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
#elif defined(__ANDROID__)
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
#elif defined(__APPLE__)
        VK_MVK_MACOS_SURFACE_EXTENSION_NAME
#endif
    };
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // Validation layers can be added here if needed

    instance = vk::createInstance(createInfo);
}

void VulkanRenderer::PickPhysicalDevice() {
    auto devices = instance.enumeratePhysicalDevices();
    if (devices.empty()) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support.");
    }
    physicalDevice = devices[0];  // For simplicity, pick the first device
}

void VulkanRenderer::CreateLogicalDevice() {
    // Find queue families
    auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    int graphicsFamily = -1;
    for (size_t i = 0; i < queueFamilies.size(); ++i) {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            graphicsFamily = static_cast<int>(i);
            break;
        }
    }
    if (graphicsFamily == -1) {
        throw std::runtime_error("Failed to find a graphics queue family.");
    }
    graphicsQueueFamily = graphicsFamily;

    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueCreateInfo;
    queueCreateInfo.queueFamilyIndex = graphicsFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // Specify device features if needed
    vk::PhysicalDeviceFeatures deviceFeatures = {};

    vk::DeviceCreateInfo createInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &deviceFeatures;

    device = physicalDevice.createDevice(createInfo);
    graphicsQueue = device.getQueue(graphicsQueueFamily, 0);
}

SwapChainHandle VulkanRenderer::CreateSwapChain(const SwapChainDesc& desc) {
    // Create surface based on platform
    CreateSurface(desc);

    CreateSwapChainInternal(desc);
    CreateImageViews();

    SwapChainHandle handle;
    handle.id = GenerateUniqueID();
    // Store swapchain and image views with handle if needed

    return handle;
}

void VulkanRenderer::CreateSurface(const SwapChainDesc& desc) {
#ifdef _WIN32
    // Win32 Surface
    VkWin32SurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = static_cast<HWND>(desc.window_handle.win32.hwnd);
    createInfo.hinstance = static_cast<HINSTANCE>(desc.window_handle.win32.hinstance);

    VkSurfaceKHR rawSurface;
    if (vkCreateWin32SurfaceKHR(static_cast<VkInstance>(instance), &createInfo, nullptr, &rawSurface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Win32 surface.");
    }
    surface = vk::SurfaceKHR(rawSurface);
#elif defined(__linux__)
    // Xlib Surface (예시)
    VkXlibSurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    createInfo.dpy = static_cast<Display*>(desc.window_handle.xlib.display);
    createInfo.window = static_cast<Window>(desc.window_handle.xlib.window);

    VkSurfaceKHR rawSurface;
    if (vkCreateXlibSurfaceKHR(static_cast<VkInstance>(instance), &createInfo, nullptr, &rawSurface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Xlib surface.");
    }
    surface = vk::SurfaceKHR(rawSurface);
#elif defined(__ANDROID__)
    // Android Surface
    VkAndroidSurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    createInfo.window = static_cast<ANativeWindow*>(desc.window_handle.android.window);

    VkSurfaceKHR rawSurface;
    if (vkCreateAndroidSurfaceKHR(static_cast<VkInstance>(instance), &createInfo, nullptr, &rawSurface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Android surface.");
    }
    surface = vk::SurfaceKHR(rawSurface);
#elif defined(__APPLE__)
    // MoltenVK Surface (macOS/iOS)
    VkMetalSurfaceCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    createInfo.pLayer = static_cast<CAMetalLayer*>(desc.window_handle.cocoa.view);

    VkSurfaceKHR rawSurface;
    if (vkCreateMetalSurfaceEXT(static_cast<VkInstance>(instance), &createInfo, nullptr, &rawSurface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Metal surface.");
    }
    surface = vk::SurfaceKHR(rawSurface);
#else
    throw std::runtime_error("Unsupported platform for surface creation.");
#endif
}

void VulkanRenderer::CreateSwapChainInternal(const SwapChainDesc& desc) {
    //  struct SwapChainDesc {
    //     void* window_handle = nullptr;
    //     int32_t width = 1280;
    //     int32_t height = 720;
    //     Format format; // unused
    //     int32_t buffer_count = 2;
    //     bool vsync = true; // unused
    //  };
    vk::SurfaceCapabilitiesKHR capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
    std::vector<vk::SurfaceFormatKHR> formats = physicalDevice.getSurfaceFormatsKHR(surface);
    std::vector<vk::PresentModeKHR> presentModes = physicalDevice.getSurfacePresentModesKHR(surface);

    // Choose surface format
    vk::SurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& availableFormat : formats) {
        if (availableFormat.format == vk::Format::eB8G8R8A8Unorm &&
            availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            surfaceFormat = availableFormat;
            break;
        }
    }

    // Choose present mode
    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;  // Default
    for (const auto& availablePresentMode : presentModes) {
        if (availablePresentMode == vk::PresentModeKHR::eMailbox) {
            presentMode = availablePresentMode;
            break;
        }
    }

    // Choose swap extent
    if (capabilities.currentExtent.width != UINT32_MAX) {
        swapchainExtent = capabilities.currentExtent;
    } else {
        swapchainExtent.width = std::max<uint32_t>(
            capabilities.minImageExtent.width,
            std::min<uint32_t>(static_cast<uint32_t>(desc.width), capabilities.maxImageExtent.width));
        swapchainExtent.height = std::max<uint32_t>(
            capabilities.minImageExtent.height,
            std::min<uint32_t>(static_cast<uint32_t>(desc.height), capabilities.maxImageExtent.height));
    }

    // Choose number of images
    uint32_t imageCount = desc.buffer_count;
    if (imageCount < capabilities.minImageCount) {
        imageCount = capabilities.minImageCount;
    }
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    // Determine image usage
    vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    if (desc.vsync) {
        // If vsync is desired, eFifo present mode is used, which is already selected
    }

    // Check if graphics and present queue families are the same
    uint32_t queueFamilyIndices[] = {graphicsQueueFamily};
    vk::SharingMode sharingMode = vk::SharingMode::eExclusive;

    // Create SwapChainCreateInfo
    vk::SwapchainCreateInfoKHR createInfo;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = imageUsage;
    createInfo.imageSharingMode = sharingMode;
    createInfo.queueFamilyIndexCount = 1;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = swapchain;

    swapchain = device.createSwapchainKHR(createInfo);
    swapchainImages = device.getSwapchainImagesKHR(swapchain);
    swapchainImageFormat = surfaceFormat.format;  // Mapping 필요
}

void VulkanRenderer::CreateImageViews() {
    swapchainImageViews.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); i++) {
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = swapchainImageFormat;  // Mapping 필요
        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        swapchainImageViews[i] = device.createImageView(viewInfo);
    }
}

BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc& desc) {
    vk::BufferCreateInfo bufferInfo;
    bufferInfo.size = desc.size;
    bufferInfo.usage = vk::BufferUsageFlags();
    if (desc.usage & BufferUsage::kVertex)
        bufferInfo.usage |= vk::BufferUsageFlagBits::eVertexBuffer;
    if (desc.usage & BufferUsage::kIndex)
        bufferInfo.usage |= vk::BufferUsageFlagBits::eIndexBuffer;
    if (desc.usage & BufferUsage::kUniform)
        bufferInfo.usage |= vk::BufferUsageFlagBits::eUniformBuffer;
    if (desc.usage & BufferUsage::kStorage)
        bufferInfo.usage |= vk::BufferUsageFlagBits::eStorageBuffer;
    if (desc.usage & BufferUsage::kIndirect)
        bufferInfo.usage |= vk::BufferUsageFlagBits::eIndirectBuffer;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkBuffer buffer;
    VmaAllocation allocation;
    if (vmaCreateBuffer(allocator, reinterpret_cast<VkBufferCreateInfo*>(&bufferInfo), &allocInfo, &buffer, &allocation,
                        nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer.");
    }

    BufferHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        buffers[handle.id] = vk::Buffer(buffer);
        bufferAllocations[handle.id] = allocation;
    }

    return handle;
}

void VulkanRenderer::UpdateBuffer(BufferHandle handle, const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = buffers.find(handle.id);
    if (it == buffers.end())
        return;

    void* mappedData;
    vmaMapMemory(allocator, bufferAllocations[handle.id], &mappedData);
    memcpy(mappedData, data, size);
    vmaUnmapMemory(allocator, bufferAllocations[handle.id]);
}

void VulkanRenderer::BindBuffer(BufferHandle handle, uint32_t bind_point, uint32_t dynamic_offset) {
    // Binding logic depends on pipeline and descriptor sets
    // This needs to be implemented based on descriptor set layout
}

TextureHandle VulkanRenderer::CreateTexture(const TextureDesc& desc) {
    vk::ImageCreateInfo imageInfo;
    imageInfo.imageType = (desc.type == TextureType::k2D)   ? vk::ImageType::e2D
                          : (desc.type == TextureType::k3D) ? vk::ImageType::e3D
                                                            : vk::ImageType::e2D;
    imageInfo.extent.width = desc.width;
    imageInfo.extent.height = desc.height;
    imageInfo.extent.depth = desc.depth;
    imageInfo.mipLevels = desc.mip_levels;
    imageInfo.arrayLayers = desc.array_layers;
    imageInfo.format = vk::Format::eR8G8B8A8Unorm;  // Mapping needed
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eSampled;
    if (desc.usage & TextureUsage::kRenderTarget)
        imageInfo.usage |= vk::ImageUsageFlagBits::eColorAttachment;
    if (desc.usage & TextureUsage::kDepthStencil)
        imageInfo.usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (desc.usage & TextureUsage::kStorage)
        imageInfo.usage |= vk::ImageUsageFlagBits::eStorage;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImage image;
    VmaAllocation allocation;
    if (vmaCreateImage(allocator, reinterpret_cast<VkImageCreateInfo*>(&imageInfo), &allocInfo, &image, &allocation,
                       nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image.");
    }

    TextureHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        images[handle.id] = vk::Image(image);
        imageAllocations[handle.id] = allocation;
    }

    // Create image view
    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = images[handle.id];
    viewInfo.viewType = (desc.type == TextureType::k2D)   ? vk::ImageViewType::e2D
                        : (desc.type == TextureType::k3D) ? vk::ImageViewType::e3D
                                                          : vk::ImageViewType::eCube;
    viewInfo.format = vk::Format::eR8G8B8A8Unorm;  // Mapping needed
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.mip_levels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc.array_layers;

    vk::ImageView imageView = device.createImageView(viewInfo);
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        imageViews[handle.id] = imageView;
    }

    return handle;
}

void VulkanRenderer::BindTexture(TextureHandle handle, uint32_t bind_point) {
    // Binding logic depends on pipeline and descriptor sets
    // This needs to be implemented based on descriptor set layout
}

SamplerHandle VulkanRenderer::CreateSampler(const SamplerDesc& desc) {
    vk::SamplerCreateInfo samplerInfo = {};
    samplerInfo.magFilter = (desc.filter_mag == Filter::kNearest) ? vk::Filter::eNearest : vk::Filter::eLinear;
    samplerInfo.minFilter = (desc.filter_min == Filter::kNearest) ? vk::Filter::eNearest : vk::Filter::eLinear;
    samplerInfo.addressModeU = (desc.address_mode_u == AddressMode::kRepeat) ? vk::SamplerAddressMode::eRepeat
                                                                             : vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = (desc.address_mode_v == AddressMode::kRepeat) ? vk::SamplerAddressMode::eRepeat
                                                                             : vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = (desc.address_mode_w == AddressMode::kRepeat) ? vk::SamplerAddressMode::eRepeat
                                                                             : vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.anisotropyEnable = desc.enable_anisotropy;
    samplerInfo.maxAnisotropy = desc.max_anisotropy;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

    vk::Sampler sampler = device.createSampler(samplerInfo);

    SamplerHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        samplers[handle.id] = sampler;
    }

    return handle;
}

void VulkanRenderer::BindSampler(SamplerHandle handle, uint32_t bind_point) {
    // Binding logic depends on pipeline and descriptor sets
    // This needs to be implemented based on descriptor set layout
}

ShaderHandle VulkanRenderer::CreateShader(const ShaderDesc& desc) {
    // Load shader code from file
    // For simplicity, assume desc.file_path contains SPIR-V binary
    std::ifstream file(desc.file_path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file.");
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    vk::ShaderModule shaderModule = CreateShaderModule(std::string(buffer.begin(), buffer.end()));

    ShaderHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        shaders[handle.id] = shaderModule;
    }

    return handle;
}

vk::ShaderModule VulkanRenderer::CreateShaderModule(const std::string& code) {
    vk::ShaderModuleCreateInfo createInfo;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    return device.createShaderModule(createInfo);
}

PipelineHandle VulkanRenderer::CreatePipeline(const PipelineDesc& desc) {
    // For simplicity, assume vertex and fragment shaders are already created and bound
    // Detailed pipeline creation involves many steps which are omitted here

    vk::GraphicsPipelineCreateInfo pipelineInfo;
    // Fill pipelineInfo based on desc
    // ...

    // Placeholder
    vk::Pipeline pipeline = device.createGraphicsPipeline(nullptr, pipelineInfo).value;

    PipelineHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        pipelines[handle.id] = pipeline;
    }

    return handle;
}

PipelineHandle VulkanRenderer::CreatePipeline(const ComputePipelineDesc& desc) {
    // Similar to CreatePipeline for graphics pipelines
    // Placeholder
    vk::ComputePipelineCreateInfo pipelineInfo;
    // Fill pipelineInfo based on desc
    // ...

    vk::Pipeline pipeline = device.createComputePipeline(nullptr, pipelineInfo).value;

    PipelineHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        pipelines[handle.id] = pipeline;
    }

    return handle;
}

void VulkanRenderer::BindPipeline(PipelineHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = pipelines.find(handle.id);
    if (it != pipelines.end()) {
        currentCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, it->second);
    }
}

void VulkanRenderer::DispatchCompute(uint32_t group_x, uint32_t group_y, uint32_t group_z) {
    currentCommandBuffer.dispatch(group_x, group_y, group_z);
}

void VulkanRenderer::BeginPass(const RenderPassDesc& desc) {
    // Create render pass based on desc
    CreateRenderPass(desc);

    vk::ClearValue clearValue;
    clearValue.color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});

    vk::RenderPassBeginInfo beginInfo;
    beginInfo.renderPass = renderPass;
    // beginInfo.framebuffer = /* Retrieve framebuffer based on swapchain image */;
    beginInfo.renderArea.offset = vk::Offset2D{0, 0};
    beginInfo.renderArea.extent = swapchainExtent;
    beginInfo.clearValueCount = 1;
    beginInfo.pClearValues = &clearValue;

    currentCommandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);
}

void VulkanRenderer::EndPass() { currentCommandBuffer.endRenderPass(); }

void VulkanRenderer::Render(const SwapChainHandle& handle, std::function<void()> callback) {
    // Acquire image from swapchain
    uint32_t imageIndex;
    device.acquireNextImageKHR(swapchain, UINT64_MAX, VK_NULL_HANDLE, nullptr, &imageIndex);

    // Begin command buffer recording
    vk::CommandBufferBeginInfo beginInfo;
    currentCommandBuffer.begin(beginInfo);

    callback();

    // End command buffer recording
    currentCommandBuffer.end();

    // Submit command buffer
    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &currentCommandBuffer;

    graphicsQueue.submit(submitInfo, VK_NULL_HANDLE);
    graphicsQueue.waitIdle();

    // Present the image using the graphics queue
    vk::PresentInfoKHR presentInfo;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    graphicsQueue.presentKHR(presentInfo);  // Correct usage of presentKHR
}

void VulkanRenderer::DrawIndexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                                 int32_t vertex_offset, uint32_t first_instance) {
    currentCommandBuffer.drawIndexed(index_count, instance_count, first_index, vertex_offset, first_instance);
}

bool VulkanRenderer::ReloadShader(ShaderHandle handle, const ShaderDesc& new_desc) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = shaders.find(handle.id);
    if (it == shaders.end())
        return false;

    // Destroy old shader module
    device.destroyShaderModule(it->second);

    // Load new shader
    std::ifstream file(new_desc.file_path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    vk::ShaderModule newModule = CreateShaderModule(std::string(buffer.begin(), buffer.end()));
    it->second = newModule;

    return true;
}

void VulkanRenderer::ReleaseResource(SwapChainHandle handle) {
    // Implement swapchain resource release
}

void VulkanRenderer::ReleaseResource(TextureHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = images.find(handle.id);
    if (it != images.end()) {
        device.destroyImageView(imageViews[handle.id]);
        vmaDestroyImage(allocator, static_cast<VkImage>(it->second), imageAllocations[handle.id]);
        images.erase(it);
        imageAllocations.erase(handle.id);
        imageViews.erase(handle.id);
    }
}

void VulkanRenderer::ReleaseResource(SamplerHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = samplers.find(handle.id);
    if (it != samplers.end()) {
        device.destroySampler(it->second);
        samplers.erase(it);
    }
}

void VulkanRenderer::ReleaseResource(PipelineHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = pipelines.find(handle.id);
    if (it != pipelines.end()) {
        device.destroyPipeline(it->second);
        pipelines.erase(it);
    }
}

void VulkanRenderer::ReleaseResource(ShaderHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = shaders.find(handle.id);
    if (it != shaders.end()) {
        device.destroyShaderModule(it->second);
        shaders.erase(it);
    }
}

void VulkanRenderer::CreateRenderPass(const RenderPassDesc& desc) {
    // Implement render pass creation based on RenderPassDesc
    // This involves creating attachments, subpasses, and dependencies
    // For simplicity, this is omitted
}
vk::Format VulkanRenderer::ToVulkanFormat(Format format) {
    switch (format) {
        case Format::kRGBA8:
            return vk::Format::eR8G8B8A8Unorm;
        case Format::kBGRA8:
            return vk::Format::eB8G8R8A8Unorm;
        case Format::kRGBA16F:
            return vk::Format::eR16G16B16A16Sfloat;
        case Format::kRGBA32F:
            return vk::Format::eR32G32B32A32Sfloat;
        case Format::kRGB8:
            return vk::Format::eR8G8B8Unorm;
        case Format::kRGB16F:
            return vk::Format::eR16G16B16Sfloat;
        case Format::kRGB32F:
            return vk::Format::eR32G32B32Sfloat;
        case Format::kDepth24Stencil8:
            return vk::Format::eD24UnormS8Uint;
        case Format::kDepth32F:
            return vk::Format::eD32Sfloat;
        case Format::kR8:
            return vk::Format::eR8Unorm;
        case Format::kR16F:
            return vk::Format::eR16Sfloat;
        case Format::kR32F:
            return vk::Format::eR32Sfloat;
        case Format::kRG8:
            return vk::Format::eR8G8Unorm;
        case Format::kRG16F:
            return vk::Format::eR16G16Sfloat;
        case Format::kRG32F:
            return vk::Format::eR32G32Sfloat;
        case Format::kSRGB8:
            return vk::Format::eR8G8B8Srgb;
        case Format::kSRGBA8:
            return vk::Format::eR8G8B8A8Srgb;
        case Format::kSRGBA8Unorm:
        case Format::kRGBA8Unorm:
            return vk::Format::eR8G8B8A8Unorm;
        case Format::kBGRA8Unorm:
            return vk::Format::eB8G8R8A8Unorm;
        case Format::kRGB8Unorm:
            return vk::Format::eR8G8B8Unorm;
        case Format::kR8Unorm:
            return vk::Format::eR8Unorm;
        case Format::kRG8Unorm:
            return vk::Format::eR8G8Unorm;
        case Format::kRGBA16Unorm:
            return vk::Format::eR16G16B16A16Unorm;
        case Format::kRGB16Unorm:
            return vk::Format::eR16G16B16Unorm;
        case Format::kR16Unorm:
            return vk::Format::eR16Unorm;
        case Format::kRG16Unorm:
            return vk::Format::eR16G16Unorm;
        default:
            return vk::Format::eUndefined;
    }
}
Format VulkanRenderer::FromVulkanFormat(vk::Format vk_format) {
    switch (vk_format) {
        case vk::Format::eR8G8B8A8Unorm:
            return Format::kRGBA8;
        case vk::Format::eB8G8R8A8Unorm:
            return Format::kBGRA8;
        case vk::Format::eR16G16B16A16Sfloat:
            return Format::kRGBA16F;
        case vk::Format::eR32G32B32A32Sfloat:
            return Format::kRGBA32F;
        case vk::Format::eR8G8B8Unorm:
            return Format::kRGB8;
        case vk::Format::eR16G16B16Sfloat:
            return Format::kRGB16F;
        case vk::Format::eR32G32B32Sfloat:
            return Format::kRGB32F;
        case vk::Format::eD24UnormS8Uint:
            return Format::kDepth24Stencil8;
        case vk::Format::eD32Sfloat:
            return Format::kDepth32F;
        case vk::Format::eR8Unorm:
            return Format::kR8;
        case vk::Format::eR16Sfloat:
            return Format::kR16F;
        case vk::Format::eR32Sfloat:
            return Format::kR32F;
        case vk::Format::eR8G8Unorm:
            return Format::kRG8;
        case vk::Format::eR16G16Sfloat:
            return Format::kRG16F;
        case vk::Format::eR32G32Sfloat:
            return Format::kRG32F;
        case vk::Format::eR8G8B8Srgb:
            return Format::kSRGB8;
        case vk::Format::eR8G8B8A8Srgb:
            return Format::kSRGBA8;
        // case vk::Format::eR8G8B8A8Unorm:
        //     return Format::kSRGBA8Unorm;
        // case vk::Format::eB8G8R8A8Unorm:
        //      return Format::kBGRA8Unorm;
        case vk::Format::eR16G16B16A16Unorm:
            return Format::kRGBA16Unorm;
        case vk::Format::eR16G16B16Unorm:
            return Format::kRGB16Unorm;
        case vk::Format::eR16Unorm:
            return Format::kR16Unorm;
        case vk::Format::eR16G16Unorm:
            return Format::kRG16Unorm;
        default:
            return Format::kUnknown;
    }
}

// Factory method
std::unique_ptr<IRenderer> IRenderer::Create() { return std::make_unique<VulkanRenderer>(); }

}  // namespace lumora
