#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>

// #define VK_USE_PLATFORM_XLIB_KHR
// #define VK_USE_PLATFORM_ANDROID_KHR
// #define VK_USE_PLATFORM_MACOS_MVK

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

// Structure to hold all Vulkan objects related to a swapchain
struct SwapChainData {
    vk::SwapchainKHR swapchain;
    std::vector<vk::Image> images;
    vk::Format imageFormat;
    vk::Extent2D extent;
    std::vector<vk::ImageView> imageViews;
    // Add other swapchain-specific data if needed (e.g., framebuffers)
};

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

    // SwapChain maps
    std::unordered_map<uint64_t, SwapChainData> swapChains;

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
    SwapChainData CreateSwapChainInternal(const SwapChainDesc& desc);
    void CreateImageViews(SwapChainData& scData);
    void CreateRenderPass(const RenderPassDesc& desc);
    void CreateCommandPool();
    void AllocateCommandBuffer();

    // Helper methods
    vk::ShaderModule CreateShaderModule(const std::string& code);

    Format FromVulkanFormat(vk::Format vk_format);

    vk::Format ToVulkanFormat(Format format);
    vk::PolygonMode ToVulkanPolygonMode(PolygonMode mode);
    vk::CullModeFlags ToVulkanCullMode(CullMode mode);
    vk::FrontFace ToVulkanFrontFace(FrontFace face);
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

    // Destroy all swapchains and their image views
    for (auto& [id, scData] : swapChains) {
        if (scData.swapchain) {
            device.destroySwapchainKHR(scData.swapchain);
        }
        for (auto& imageView : scData.imageViews) {
            device.destroyImageView(imageView);
        }
    }
    swapChains.clear();

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

    // Select the first suitable device
    for (const auto& deviceCandidate : devices) {
        // Add more comprehensive checks here (e.g., required features, queue families)
        auto queueFamilies = deviceCandidate.getQueueFamilyProperties();
        bool hasGraphics = false;
        for (size_t i = 0; i < queueFamilies.size(); ++i) {
            if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                hasGraphics = true;
                graphicsQueueFamily = static_cast<uint32_t>(i);
                break;
            }
        }
        if (hasGraphics) {
            physicalDevice = deviceCandidate;
            break;
        }
    }

    if (!physicalDevice) {
        throw std::runtime_error("Failed to find a suitable GPU.");
    }
}

void VulkanRenderer::CreateLogicalDevice() {
    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueCreateInfo;
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    vk::PhysicalDeviceFeatures deviceFeatures = {};

    vk::DeviceCreateInfo createInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &deviceFeatures;

    // Enable device extensions if needed

    device = physicalDevice.createDevice(createInfo);
    graphicsQueue = device.getQueue(graphicsQueueFamily, 0);
}

SwapChainHandle VulkanRenderer::CreateSwapChain(const SwapChainDesc& desc) {
    std::lock_guard<std::mutex> lock(resourceMutex);

    // Create surface for this swapchain if not already created
    CreateSurface(desc);

    // Create internal swapchain data
    SwapChainData scData = CreateSwapChainInternal(desc);

    // Store the swapchain data
    SwapChainHandle handle;
    handle.id = GenerateUniqueID();
    swapChains[handle.id] = scData;

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
    // Xlib Surface (example)
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
    createInfo.pLayer = static_cast<id<CAMetalLayer> >(desc.window_handle.cocoa.view);

    VkSurfaceKHR rawSurface;
    if (vkCreateMetalSurfaceEXT(static_cast<VkInstance>(instance), &createInfo, nullptr, &rawSurface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Metal surface.");
    }
    surface = vk::SurfaceKHR(rawSurface);
#else
    throw std::runtime_error("Unsupported platform for surface creation.");
#endif
}

SwapChainData VulkanRenderer::CreateSwapChainInternal(const SwapChainDesc& desc) {
    //  struct SwapChainDesc {
    //     void* window_handle = nullptr;
    //     int32_t width = 1280;
    //     int32_t height = 720;
    //     Format format; // unused
    //     int32_t buffer_count = 2;
    //     bool vsync = true; // unused
    //  };
    SwapChainData scData;

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
        scData.extent = capabilities.currentExtent;
    } else {
        scData.extent.width = std::max<uint32_t>(
            capabilities.minImageExtent.width,
            std::min<uint32_t>(static_cast<uint32_t>(desc.width), capabilities.maxImageExtent.width));
        scData.extent.height = std::max<uint32_t>(
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
    // Add additional usage flags based on SwapChainDesc or application needs

    // Check if graphics and present queue families are the same
    uint32_t queueFamilyIndices[] = {graphicsQueueFamily};
    vk::SharingMode sharingMode = vk::SharingMode::eExclusive;

    // Create SwapChainCreateInfo
    vk::SwapchainCreateInfoKHR createInfo;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = scData.extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = imageUsage;
    createInfo.imageSharingMode = sharingMode;
    createInfo.queueFamilyIndexCount = 1;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = nullptr;  // Handle old swapchain if recreating

    scData.swapchain = device.createSwapchainKHR(createInfo);
    scData.images = device.getSwapchainImagesKHR(scData.swapchain);
    scData.imageFormat = ToVulkanFormat(desc.format);

    // Create Image Views
    CreateImageViews(scData);

    return scData;
}

void VulkanRenderer::CreateImageViews(SwapChainData& scData) {
    scData.imageViews.resize(scData.images.size());

    for (size_t i = 0; i < scData.images.size(); i++) {
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = scData.images[i];
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = scData.imageFormat;
        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        scData.imageViews[i] = device.createImageView(viewInfo);
    }
}

BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc& desc) {
    vk::BufferCreateInfo bufferInfo;
    bufferInfo.size = desc.size;
    bufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;  // Map usage based on desc.usage
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
    // Map MemoryUsage to VMA_MEMORY_USAGE
    switch (desc.memory_usage) {
        case MemoryUsage::kGpuOnly:
            allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            break;
        case MemoryUsage::kCpuToGpu:
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            break;
        case MemoryUsage::kAuto:
        default:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            break;
    }

    VkBuffer buffer;
    VmaAllocation allocation;
    if (vmaCreateBuffer(allocator, reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &allocInfo, &buffer,
                        &allocation, nullptr) != VK_SUCCESS) {
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
    imageInfo.format = ToVulkanFormat(desc.format);
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eSampled;
    if (desc.usage & TextureUsage::kRenderTarget)
        imageInfo.usage |= vk::ImageUsageFlagBits::eColorAttachment;
    if (desc.usage & TextureUsage::kDepthStencil)
        imageInfo.usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (desc.usage & TextureUsage::kStorage)
        imageInfo.usage |= vk::ImageUsageFlagBits::eStorage;
    if (desc.usage & TextureUsage::kInputAttachment)
        imageInfo.usage |= vk::ImageUsageFlagBits::eInputAttachment;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImage image;
    VmaAllocation allocation;
    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &image,
                       &allocation, nullptr) != VK_SUCCESS) {
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
    viewInfo.format = ToVulkanFormat(desc.format);
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
    // Create shader stages
    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = shaders[desc.vertex_shader.id];
    vertShaderStageInfo.pName = "main";
    shaderStages.push_back(vertShaderStageInfo);

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = shaders[desc.fragment_shader.id];
    fragShaderStageInfo.pName = "main";
    shaderStages.push_back(fragShaderStageInfo);

    // Vertex input
    std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
    std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    // Populate bindingDescriptions and attributeDescriptions based on desc.vertex_layout_desc

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    vk::Viewport viewport = {};
    viewport.x = desc.viewport.x;
    viewport.y = desc.viewport.y;
    viewport.width = desc.viewport.width;
    viewport.height = desc.viewport.height;
    viewport.minDepth = desc.viewport.min_depth;
    viewport.maxDepth = desc.viewport.max_depth;

    vk::Rect2D scissor = {};
    scissor.offset = vk::Offset2D{desc.scissor.offset_x, desc.scissor.offset_y};
    scissor.extent = vk::Extent2D{desc.scissor.width, desc.scissor.height};

    vk::PipelineViewportStateCreateInfo viewportState = {};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    vk::PipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.depthClampEnable = desc.rasterization.depth_clamp_enable;
    rasterizer.rasterizerDiscardEnable = desc.rasterization.rasterizer_discard_enable;
    rasterizer.polygonMode = static_cast<vk::PolygonMode>(desc.rasterization.polygon_mode);
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = ToVulkanCullMode(desc.rasterization.cull_mode);
    rasterizer.frontFace = ToVulkanFrontFace(desc.rasterization.front_face);
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Color blending
    std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
    for (const auto& blendState : desc.color_blends) {
        vk::PipelineColorBlendAttachmentState colorBlend = {};
        colorBlend.blendEnable = blendState.blend_enable;
        colorBlend.srcColorBlendFactor = static_cast<vk::BlendFactor>(blendState.src_color_blend_factor);
        colorBlend.dstColorBlendFactor = static_cast<vk::BlendFactor>(blendState.dst_color_blend_factor);
        colorBlend.colorBlendOp = static_cast<vk::BlendOp>(blendState.color_blend_op);
        colorBlend.srcAlphaBlendFactor = static_cast<vk::BlendFactor>(blendState.src_alpha_blend_factor);
        colorBlend.dstAlphaBlendFactor = static_cast<vk::BlendFactor>(blendState.dst_alpha_blend_factor);
        colorBlend.alphaBlendOp = static_cast<vk::BlendOp>(blendState.alpha_blend_op);
        colorBlend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                    vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        colorBlendAttachments.push_back(colorBlend);
    }

    vk::PipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = vk::LogicOp::eCopy;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.setLayoutCount = 0;  // Adjust based on descriptor sets
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;  // Adjust if using push constants
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    pipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);

    // Pipeline creation
    vk::GraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr;  // Implement if using depth
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;  // Implement if using dynamic states
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr;

    vk::Pipeline graphicsPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo).value;

    // Store the pipeline
    PipelineHandle handle;
    handle.id = GenerateUniqueID();
    pipelines[handle.id] = graphicsPipeline;

    return handle;
}

PipelineHandle VulkanRenderer::CreatePipeline(const ComputePipelineDesc& desc) {
    // Create shader stage
    vk::PipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    shaderStageInfo.module = shaders[desc.compute_shader.id];
    shaderStageInfo.pName = "main";

    // Pipeline layout (reuse existing or create new)
    // For simplicity, using the existing pipelineLayout
    // Adjust based on descriptor sets and push constants
    vk::ComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.basePipelineHandle = nullptr;

    vk::Pipeline computePipeline = device.createComputePipeline(nullptr, pipelineInfo).value;

    // Store the pipeline
    PipelineHandle handle;
    handle.id = GenerateUniqueID();
    pipelines[handle.id] = computePipeline;

    return handle;
}

void VulkanRenderer::BindPipeline(PipelineHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = pipelines.find(handle.id);
    if (it != pipelines.end()) {
        vk::PipelineBindPoint bindPoint = vk::PipelineBindPoint::eGraphics;
        // Determine bind point based on pipeline type if needed
        // For simplicity, assuming graphics pipeline
        currentCommandBuffer.bindPipeline(bindPoint, it->second);
    }
}

void VulkanRenderer::DispatchCompute(uint32_t group_x, uint32_t group_y, uint32_t group_z) {
    currentCommandBuffer.dispatch(group_x, group_y, group_z);
}

void VulkanRenderer::BeginPass(const RenderPassDesc& desc) {
    // This implementation assumes that the current swapchain is set externally
    // Alternatively, modify to accept a swapchain handle
    // For this example, assume single render pass setup

    // Define clear values based on RenderPassDesc
    std::vector<vk::ClearValue> clearValues;
    for (const auto& color : desc.clear_colors) {
        clearValues.emplace_back(vk::ClearValue(vk::ClearColorValue({color[0], color[1], color[2], color[3]})));
    }
    if (desc.clear_depth) {
        vk::ClearDepthStencilValue depthClear = {};
        depthClear.depth = desc.clear_depth_value;
        depthClear.stencil = desc.clear_stencil_value;
        clearValues.emplace_back(vk::ClearValue(depthClear));
    }

    // For this example, assume a single subpass
    vk::RenderPassBeginInfo renderPassInfo;
    renderPassInfo.renderPass = renderPass;
    // renderPassInfo.framebuffer = /* Retrieve framebuffer based on current swapchain image */;
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    // renderPassInfo.renderArea.extent = /* Swapchain extent */;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    currentCommandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
}

void VulkanRenderer::EndPass() { currentCommandBuffer.endRenderPass(); }

void VulkanRenderer::Render(const SwapChainHandle& handle, std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = swapChains.find(handle.id);
    if (it == swapChains.end()) {
        throw std::runtime_error("Invalid SwapChainHandle.");
    }

    SwapChainData& scData = it->second;

    // Acquire image from swapchain
    vk::ResultValue<uint32_t> acquireResult =
        device.acquireNextImageKHR(scData.swapchain, UINT64_MAX, nullptr, nullptr);
    if (acquireResult.result != vk::Result::eSuccess && acquireResult.result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("Failed to acquire swapchain image.");
    }
    uint32_t imageIndex = acquireResult.value;

    // Begin command buffer recording
    currentCommandBuffer.begin({vk::CommandBufferUsageFlagBits::eSimultaneousUse});

    // Begin render pass
    vk::RenderPassBeginInfo renderPassInfo;
    renderPassInfo.renderPass = renderPass;
    // renderPassInfo.framebuffer = /* Retrieve framebuffer based on scData.imageViews[imageIndex] */;
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = scData.extent;

    // Define clear values
    std::vector<vk::ClearValue> clearValues;
    // Assuming single color attachment
    clearValues.emplace_back(vk::ClearValue(vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f})));
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    currentCommandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Execute callback (user-defined rendering commands)
    callback();

    // End render pass
    currentCommandBuffer.endRenderPass();

    // End command buffer recording
    currentCommandBuffer.end();

    // Submit command buffer
    vk::SubmitInfo submitInfo = {};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &currentCommandBuffer;

    graphicsQueue.submit(submitInfo, VK_NULL_HANDLE);
    graphicsQueue.waitIdle();

    // Present the image
    vk::PresentInfoKHR presentInfo = {};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &scData.swapchain;
    presentInfo.pImageIndices = &imageIndex;

    std::ignore = graphicsQueue.presentKHR(presentInfo);  // Correct usage of presentKHR
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
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = swapChains.find(handle.id);
    if (it != swapChains.end()) {
        SwapChainData& scData = it->second;
        if (scData.swapchain) {
            device.destroySwapchainKHR(scData.swapchain);
        }
        for (auto& imageView : scData.imageViews) {
            device.destroyImageView(imageView);
        }
        // Destroy framebuffers if implemented
        swapChains.erase(it);
    }
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
    // For simplicity, create a basic render pass with one color attachment
    vk::AttachmentDescription colorAttachment;
    colorAttachment.format = vk::Format::eR8G8B8A8Unorm;  // This should map from desc.color_targets
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference colorAttachmentRef;
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    vk::SubpassDependency dependency;
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask = vk::AccessFlags();
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    vk::RenderPassCreateInfo renderPassInfo;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    renderPass = device.createRenderPass(renderPassInfo);
}

void VulkanRenderer::CreateCommandPool() {
    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

    commandPool = device.createCommandPool(poolInfo);
}

void VulkanRenderer::AllocateCommandBuffer() {
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.commandPool = commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    auto commandBuffers = device.allocateCommandBuffers(allocInfo);
    if (commandBuffers.empty()) {
        throw std::runtime_error("Failed to allocate command buffer.");
    }

    currentCommandBuffer = commandBuffers[0];
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
            return vk::Format::eR8G8B8A8Unorm;
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
        //     return Format::kBGRA8Unorm;
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

// Converts PolygonMode to vk::PolygonMode.
vk::PolygonMode VulkanRenderer::ToVulkanPolygonMode(PolygonMode mode) {
    switch (mode) {
        case PolygonMode::kFill:
            return vk::PolygonMode::eFill;
        case PolygonMode::kLine:
            return vk::PolygonMode::eLine;
        case PolygonMode::kPoint:
            return vk::PolygonMode::ePoint;
        default:
            throw std::runtime_error("Invalid PolygonMode.");
    }
}

// Converts CullMode to vk::CullModeFlags.
vk::CullModeFlags VulkanRenderer::ToVulkanCullMode(CullMode mode) {
    switch (mode) {
        case CullMode::kNone:
            return vk::CullModeFlagBits::eNone;
        case CullMode::kFront:
            return vk::CullModeFlagBits::eFront;
        case CullMode::kBack:
            return vk::CullModeFlagBits::eBack;
        case CullMode::kFrontAndBack:
            return vk::CullModeFlagBits::eFrontAndBack;
        default:
            throw std::runtime_error("Invalid CullMode.");
    }
}

// Converts FrontFace to vk::FrontFace.
vk::FrontFace VulkanRenderer::ToVulkanFrontFace(FrontFace face) {
    switch (face) {
        case FrontFace::kCcw:
            return vk::FrontFace::eCounterClockwise;
        case FrontFace::kCw:
            return vk::FrontFace::eClockwise;
        default:
            throw std::runtime_error("Invalid FrontFace.");
    }
}

// Factory method
std::unique_ptr<IRenderer> IRenderer::Create() { return std::make_unique<VulkanRenderer>(); }

}  // namespace lumora
