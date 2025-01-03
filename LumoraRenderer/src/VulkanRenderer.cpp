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

// For debug messenger
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                             VkDebugUtilsMessageTypeFlagsEXT messageType,
                                             const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                             void* pUserData) {
    std::cerr << "Validation Layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}

namespace lumora {

// Constants
const int MAX_FRAMES_IN_FLIGHT = 2;

// #TODO HashUtils 네임 스페이스를 없고 각각의 hashDesc 오버로딩 메소드 하나로 통일한다.
// Simple hash combine function
inline void hash_combine(std::size_t& seed) {}

template <typename T, typename... Rest>
inline void hash_combine(std::size_t& seed, const T& v, Rest... rest) {
    seed ^= std::hash<T>()(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    hash_combine(seed, rest...);
}

// Hash FrameBufferDesc
inline uint64_t hashDesc(const FrameBufferDesc& desc) {
    std::size_t seed = 0;
    for (const auto& target : desc.color_targets) {
        hash_combine(seed, target.id);
    }
    hash_combine(seed, desc.depth_target.id);
    hash_combine(seed, desc.width);
    hash_combine(seed, desc.height);
    return static_cast<uint64_t>(seed);
}

// Hash RenderPassDesc
inline uint64_t hashDesc(const RenderPassDesc& desc) {
    std::size_t seed = 0;
    for (const auto& target : desc.color_targets) {
        hash_combine(seed, target.id);
    }
    hash_combine(seed, desc.depth_target.id);
    return static_cast<uint64_t>(seed);
}

struct HandleHash {
    std::size_t operator()(const ResourceHandle& handle) const { return static_cast<std::size_t>(handle.id); }
};

// Helper function to generate unique IDs for handles
static uint64_t GenerateUniqueID() {
    static uint64_t current_id = 1;
    return current_id++;
}

// Internal Resource Handle
struct FrameBufferHandle : ResourceHandle {
    uint64_t id;
};

struct RenderPassHandle : ResourceHandle {
    uint64_t id;
};

// Internal Descs
struct FrameBufferDesc {
    std::vector<TextureHandle> color_targets;
    TextureHandle depth_target;
    uint32_t width;
    uint32_t height;
};

// Resource Structs
struct VulkanResource {
    uint32_t refCount = 1;
};

struct VulkanBuffer : VulkanResource {
    vk::Buffer buffer;
    VmaAllocation allocation;
    vk::DeviceSize size;
    vk::BufferUsageFlags usage;
    MemoryUsage memoryUsage;
};

struct VulkanTexture : VulkanResource {
    vk::Image image;
    VmaAllocation allocation;
    vk::ImageView imageView;
    vk::Format format;
    vk::Extent3D extent;
    uint32_t mipLevels;
    uint32_t arrayLayers;
    TextureUsage usage;
};

struct VulkanSampler : VulkanResource {
    vk::Sampler sampler;
    SamplerDesc desc;  // To store sampler configuration
};

struct VulkanShader : VulkanResource {
    vk::ShaderModule shaderModule;
    ShaderDesc desc;  // To store shader metadata
};

struct VulkanPipeline : VulkanResource {
    vk::Pipeline pipeline;
    vk::PipelineLayout layout;
    PipelineDesc desc;  // To store pipeline configuration
};

// New Structs for Framebuffer and Render Pass
struct VulkanFrameBuffer : VulkanResource {
    vk::Framebuffer framebuffer;
    RenderPassHandle renderPassHandle;
    FrameBufferDesc desc;  // To store framebuffer description
};

struct VulkanRenderPass : VulkanResource {
    vk::RenderPass renderPass;
    RenderPassDesc desc;  // To store render pass description
};

struct VulkanSwapChain : VulkanResource {
    vk::SwapchainKHR swapchain;
    std::vector<vk::Image> images;
    vk::Format imageFormat;
    vk::Extent2D extent;
    std::vector<vk::ImageView> imageViews;
    FrameBufferHandle frameBufferHandle;
    RenderPassHandle renderPassHandle;
    // Synchronization primitives
    std::vector<vk::Semaphore> imageAvailableSemaphores;
    std::vector<vk::Semaphore> renderFinishedSemaphores;
    std::vector<vk::Fence> inFlightFences;
    size_t currentFrame;
};

// Descriptor Set Management Structures
struct DescriptorSetLayoutInfo {
    vk::DescriptorSetLayoutBinding binding;
    vk::DescriptorType type;
    vk::ShaderStageFlags stageFlags;
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
    vk::CommandPool commandPool;
    vk::CommandBuffer commandBuffer;

    // Debug messenger
    vk::DebugUtilsMessengerEXT debugMessenger;

    // VMA Allocator
    VmaAllocator allocator;

    // Resource maps using dedicated structs
    std::unordered_map<BufferHandle, VulkanBuffer, HandleHash> buffers;
    std::unordered_map<TextureHandle, VulkanTexture, HandleHash> textures;
    std::unordered_map<SamplerHandle, VulkanSampler, HandleHash> samplers;
    std::unordered_map<ShaderHandle, VulkanShader, HandleHash> shaders;
    std::unordered_map<PipelineHandle, VulkanPipeline, HandleHash> pipelines;
    std::unordered_map<SwapChainHandle, VulkanSwapChain, HandleHash> swapChains;
    std::unordered_map<FrameBufferHandle, VulkanFrameBuffer, HandleHash> frameBuffers;
    std::unordered_map<RenderPassHandle, VulkanRenderPass, HandleHash> renderPasses;

    // Handle to index mapping
    std::mutex resourceMutex;

    // Descriptor Set Management
    vk::DescriptorPool descriptorPool;
    std::mutex descriptorMutex;

    // Current pipeline handle
    vk::Pipeline currentPipeline;

    // Internal methods
    void InitVulkan(const char* app_name);
    void CleanupVulkan();
    void CreateInstance(const char* app_name);
    void SetupDebugMessenger();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSurface(const SwapChainDesc& desc);
    RenderPassHandle CreateRenderPassInternal(const RenderPassDesc& desc);
    FrameBufferHandle CreateFrameBufferInternal(const FrameBufferDesc& desc);
    void CreateCommandPool();
    void AllocateCommandBuffer();
    void CreateDescriptorPool();
    void SetupSynchronization(VulkanSwapChain& scData);
    void CleanupSynchronization(VulkanSwapChain& scData);
    void ReleaseResource(const RenderPassHandle& handle);
    void ReleaseResource(const FrameBufferHandle& handle);

    // Descriptor Set Management Methods
    vk::DescriptorSetLayout CreateDescriptorSetLayout(const std::vector<DescriptorSetLayoutInfo>& bindings);
    vk::DescriptorSet AllocateDescriptorSet(vk::DescriptorSetLayout layout);
    void UpdateDescriptorSet(vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type,
                             const vk::DescriptorImageInfo& imageInfo);
    void UpdateDescriptorSet(vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type,
                             const vk::DescriptorBufferInfo& bufferInfo);

    // Helper methods
    vk::ShaderModule CreateShaderModule(const std::vector<char>& code);
    bool CheckValidationLayerSupport();
    std::vector<const char*> GetRequiredExtensions();

    void TransitionImageLayout(vk::Image image, vk::Format format, vk::ImageLayout oldLayout,
                               vk::ImageLayout newLayout);
    void InsertImageMemoryBarrier(vk::CommandBuffer& cmdBuffer, vk::Image image, vk::Format format,
                                  vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::PipelineStageFlags srcStage,
                                  vk::PipelineStageFlags dstStage);

    Format MapFormat(vk::Format vk_format);
    vk::Format MapFormat(Format format);
    vk::PolygonMode MapFormat(PolygonMode mode);
    vk::CullModeFlags MapFormat(CullMode mode);
    vk::FrontFace MapFormat(FrontFace face);
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
    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator.");
    }

    // Create Command Pool
    CreateCommandPool();

    // Allocate Command Buffer
    AllocateCommandBuffer();

    // Create Descriptor Pool
    CreateDescriptorPool();

    // Create Render Pass with default settings
    RenderPassDesc defaultPassDesc;
    // Initialize defaultPassDesc as needed
    // VulkanRenderPass vRenderPass = CreateRenderPassInternal(defaultPassDesc);
    // renderPasses[hashDesc(defaultPassDesc)] = vRenderPass;
    // renderPass = vRenderPass.renderPass;
}

void VulkanRenderer::CleanupVulkan() {
    std::lock_guard<std::mutex> lock(resourceMutex);
    bool memoryLeak = false;

    // Destroy all pipelines
    for (auto& [handle, pipeline] : pipelines) {
        device.destroyPipeline(pipeline.pipeline);
        device.destroyPipelineLayout(pipeline.layout);
        if (pipeline.refCount != 0) {
            memoryLeak = true;
            std::cerr << "Memory Leak: Pipeline ID " << handle.id << " has refCount " << pipeline.refCount << std::endl;
        }
    }
    pipelines.clear();

    // Destroy all shader modules
    for (auto& [handle, shader] : shaders) {
        device.destroyShaderModule(shader.shaderModule);
        if (shader.refCount != 0) {
            memoryLeak = true;
            std::cerr << "Memory Leak: Shader ID " << handle.id << " has refCount " << shader.refCount << std::endl;
        }
    }
    shaders.clear();

    // Destroy all samplers
    for (auto& [handle, sampler] : samplers) {
        device.destroySampler(sampler.sampler);
        if (sampler.refCount != 0) {
            memoryLeak = true;
            std::cerr << "Memory Leak: Sampler ID " << handle.id << " has refCount " << sampler.refCount << std::endl;
        }
    }
    samplers.clear();

    // Destroy all image views and images
    for (auto& [handle, texture] : textures) {
        device.destroyImageView(texture.imageView);
        vmaDestroyImage(allocator, static_cast<VkImage>(texture.image), texture.allocation);
        if (texture.refCount != 0) {
            memoryLeak = true;
            std::cerr << "Memory Leak: Texture ID " << handle.id << " has refCount " << texture.refCount << std::endl;
        }
    }
    textures.clear();

    // Destroy all buffers
    for (auto& [handle, buffer] : buffers) {
        vmaDestroyBuffer(allocator, static_cast<VkBuffer>(buffer.buffer), buffer.allocation);
        if (buffer.refCount != 0) {
            memoryLeak = true;
            std::cerr << "Memory Leak: Buffer ID " << handle.id << " has refCount " << buffer.refCount << std::endl;
        }
    }
    buffers.clear();

    // Destroy all framebuffers
    for (auto& [handle, framebuffer] : frameBuffers) {
        device.destroyFramebuffer(framebuffer.framebuffer);
        if (framebuffer.refCount != 0) {
            memoryLeak = true;
            std::cerr << "Memory Leak: FrameBuffer ID " << handle.id << " has refCount " << framebuffer.refCount
                      << std::endl;
        }
    }
    frameBuffers.clear();

    // Destroy all render passes
    for (auto& [handle, renderPassStruct] : renderPasses) {
        device.destroyRenderPass(renderPassStruct.renderPass);
        if (renderPassStruct.refCount != 0) {
            memoryLeak = true;
            std::cerr << "Memory Leak: RenderPass ID " << handle.id << " has refCount " << renderPassStruct.refCount
                      << std::endl;
        }
    }
    renderPasses.clear();

    // Destroy all swapchains and their image views and framebuffers
    for (auto& [handle, scData] : swapChains) {
        if (scData.swapchain) {
            device.destroySwapchainKHR(scData.swapchain);
        }
        ReleaseResource(scData.frameBufferHandle);
        ReleaseResource(scData.renderPassHandle);

        for (auto& imageView : scData.imageViews) {
            device.destroyImageView(imageView);
        }
        // Destroy synchronization primitives
        for (auto& semaphore : scData.imageAvailableSemaphores) {
            device.destroySemaphore(semaphore);
        }
        for (auto& semaphore : scData.renderFinishedSemaphores) {
            device.destroySemaphore(semaphore);
        }
        for (auto& fence : scData.inFlightFences) {
            device.destroyFence(fence);
        }
    }
    swapChains.clear();

    if (memoryLeak) {
        std::cerr << "VulkanRenderer Cleanup: Memory leaks detected." << std::endl;
    }

    // Destroy Render Pass
    if (renderPass) {
        device.destroyRenderPass(renderPass);
    }

    // Destroy Command Pool
    if (commandPool) {
        device.destroyCommandPool(commandPool);
    }

    // Destroy Descriptor Pool
    if (descriptorPool) {
        device.destroyDescriptorPool(descriptorPool);
    }

    // Destroy Debug Messenger
    if (debugMessenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(static_cast<VkInstance>(instance),
                                                                               "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(static_cast<VkInstance>(instance), static_cast<VkDebugUtilsMessengerEXT>(debugMessenger), nullptr);
        }
    }

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
    // Validation layers
    const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};

    if (!CheckValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested, but not available!");
    }

    // Application info
    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = app_name;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Lumora";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    // Get required extensions
    std::vector<const char*> extensions = GetRequiredExtensions();

    vk::InstanceCreateInfo createInfo;
    createInfo.pApplicationInfo = &appInfo;

    // Enable extensions
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // Enable validation layers
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    // Debug messenger create info (optional)
    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
    debugCreateInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                                      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                      vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    debugCreateInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                  vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                  vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    debugCreateInfo.pfnUserCallback = debugCallback;

    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;

    // Create instance
    try {
        instance = vk::createInstance(createInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create Vulkan instance: ") + e.what());
    }
}

void VulkanRenderer::SetupDebugMessenger() {
    if (!CheckValidationLayerSupport())
        return;

    vk::DebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                                 vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                 vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    createInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                             vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                             vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    createInfo.pfnUserCallback = debugCallback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(static_cast<VkInstance>(instance),
                                                                          "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        VkDebugUtilsMessengerEXT messenger;
        if (func(static_cast<VkInstance>(instance),
                 reinterpret_cast<const VkDebugUtilsMessengerCreateInfoEXT*>(&createInfo), nullptr,
                 &messenger) != VK_SUCCESS) {
            throw std::runtime_error("Failed to set up debug messenger!");
        }
        debugMessenger = vk::DebugUtilsMessengerEXT(messenger);
    } else {
        throw std::runtime_error("Could not load vkCreateDebugUtilsMessengerEXT");
    }
}

void VulkanRenderer::PickPhysicalDevice() {
    auto devices = instance.enumeratePhysicalDevices();
    if (devices.empty()) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support.");
    }

    // Select the first suitable device
    for (const auto& deviceCandidate : devices) {
        // Check for graphics queue family
        auto queueFamilies = deviceCandidate.getQueueFamilyProperties();
        bool hasGraphics = false;
        for (size_t i = 0; i < queueFamilies.size(); ++i) {
            if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                // Check if the device supports the surface
                auto surfaceFormats = deviceCandidate.getSurfaceFormatsKHR(surface);
                auto presentModes = deviceCandidate.getSurfacePresentModesKHR(surface);
                if (!surfaceFormats.empty() && !presentModes.empty()) {
                    graphicsQueueFamily = static_cast<uint32_t>(i);
                    hasGraphics = true;
                    break;
                }
            }
        }
        if (hasGraphics) {
            physicalDevice = deviceCandidate;
            break;
        }
    }

    if (!physicalDevice) {
        throw std::runtime_error("Failed to find a suitable GPU with graphics and present capabilities.");
    }
}

void VulkanRenderer::CreateLogicalDevice() {
    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueCreateInfo;
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // Specify device features if needed
    vk::PhysicalDeviceFeatures deviceFeatures = {};

    // Device extensions
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
        // Add other device extensions if needed
    };

    vk::DeviceCreateInfo createInfo;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Enable validation layers for device (optional, deprecated in newer Vulkan)
    createInfo.enabledLayerCount = 0;

    try {
        device = physicalDevice.createDevice(createInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create logical device: ") + e.what());
    }

    graphicsQueue = device.getQueue(graphicsQueueFamily, 0);
}

SwapChainHandle VulkanRenderer::CreateSwapChain(const SwapChainDesc& desc) {
    std::lock_guard<std::mutex> lock(resourceMutex);

    // Create surface if not already created
    if (!surface) {
        CreateSurface(desc);
    }

    // Create internal swapchain data
    SwapChainHandle handle;

    handle.id = GenerateUniqueID();

    VulkanSwapChain swapChain;

    // Create textures based on SwapChainDesc
    // VulkanTexture colorTexture = CreateTexture(desc.colorAttachmentOptions);
    // VulkanTexture depthTexture = CreateTexture(desc.depthAttachmentOptions);

    // Create render pass and frame buffer
    RenderPassDesc renderPassDesc;
    RenderPassHandle renderPassHandle = CreateRenderPassInternal(renderPassDesc);
    FrameBufferDesc frameBufferDesc;
    // frameBufferDesc.renderPassHandle = renderPassHandle;
    // frameBufferDesc.colorAttachments.push_back(colorTexture.handle);
    // frameBufferDesc.depthAttachment = depthTexture.handle;

    FrameBufferHandle frameBufferHandle = CreateFrameBufferInternal(frameBufferDesc);

    swapChain.renderPassHandle = renderPassHandle;
    swapChain.frameBufferHandle = frameBufferHandle;
    swapChains[handle] = swapChain;

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

RenderPassHandle VulkanRenderer::CreateRenderPassInternal(const RenderPassDesc& desc) {
    // Hash the RenderPassDesc to use as a key
    uint64_t hashKey = hashDesc(desc);

    // Check if render pass already exists
    auto it = renderPasses.find(RenderPassHandle{hashKey});
    if (it != renderPasses.end()) {
        // Increment refCount and return existing render pass
        it->second.refCount++;
        return it->first;
    }

    // Determine number of attachments based on desc.color_targets and depth_target
    size_t attachmentCount = desc.color_targets.size();
    bool hasDepth = desc.depth_target.id != 0;  // Assuming TextureHandle{0} is invalid

    if (hasDepth) {
        attachmentCount += 1;
    }

    std::vector<vk::AttachmentDescription> attachments(attachmentCount);
    std::vector<vk::AttachmentReference> colorAttachmentRefs(desc.color_targets.size());
    std::vector<vk::AttachmentReference> depthAttachmentRef;

    // Setup color attachments
    for (size_t i = 0; i < desc.color_targets.size(); i++) {
        const auto& colorTarget = desc.color_targets[i];
        auto textureIt = textures.find(colorTarget);
        if (textureIt == textures.end()) {
            throw std::runtime_error("Invalid ColorTargetHandle in RenderPassDesc.");
        }

        attachments[i].format = textureIt->second.format;
        attachments[i].samples = vk::SampleCountFlagBits::e1;
        attachments[i].loadOp =
            (desc.color_attachment_options[i].load_op == AttachmentLoadOp::kClear)  ? vk::AttachmentLoadOp::eClear
            : (desc.color_attachment_options[i].load_op == AttachmentLoadOp::kLoad) ? vk::AttachmentLoadOp::eLoad
                                                                                    : vk::AttachmentLoadOp::eDontCare;
        attachments[i].storeOp = (desc.color_attachment_options[i].store_op == AttachmentStoreOp::kStore)
                                     ? vk::AttachmentStoreOp::eStore
                                     : vk::AttachmentStoreOp::eDontCare;
        attachments[i].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachments[i].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachments[i].initialLayout = vk::ImageLayout::eUndefined;
        attachments[i].finalLayout = vk::ImageLayout::ePresentSrcKHR;

        colorAttachmentRefs[i].attachment = static_cast<uint32_t>(i);
        colorAttachmentRefs[i].layout = vk::ImageLayout::eColorAttachmentOptimal;
    }

    // Setup depth attachment if present
    if (hasDepth) {
        const auto& depthAttachment = desc.depth_target;

        auto textureIt = textures.find(depthAttachment);
        if (textureIt == textures.end()) {
            throw std::runtime_error("Invalid DepthTargetHandle in RenderPassDesc.");
        }

        attachments[desc.color_targets.size()].format = vk::Format::eD32Sfloat;  // Example format, map appropriately
        attachments[desc.color_targets.size()].samples = vk::SampleCountFlagBits::e1;
        attachments[desc.color_targets.size()].loadOp =
            (desc.depth_attachment_options.load_op == AttachmentLoadOp::kClear)  ? vk::AttachmentLoadOp::eClear
            : (desc.depth_attachment_options.load_op == AttachmentLoadOp::kLoad) ? vk::AttachmentLoadOp::eLoad
                                                                                 : vk::AttachmentLoadOp::eDontCare;
        attachments[desc.color_targets.size()].storeOp =
            (desc.depth_attachment_options.store_op == AttachmentStoreOp::kStore) ? vk::AttachmentStoreOp::eStore
                                                                                  : vk::AttachmentStoreOp::eDontCare;
        attachments[desc.color_targets.size()].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachments[desc.color_targets.size()].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachments[desc.color_targets.size()].initialLayout = vk::ImageLayout::eUndefined;
        attachments[desc.color_targets.size()].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthRef{};
        depthRef.attachment = static_cast<uint32_t>(desc.color_targets.size());
        depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        depthAttachmentRef.push_back(depthRef);
    }

    // Define subpasses
    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = static_cast<uint32_t>(desc.color_targets.size());
    subpass.pColorAttachments = colorAttachmentRefs.data();
    if (hasDepth) {
        subpass.pDepthStencilAttachment = &depthAttachmentRef[0];
    } else {
        subpass.pDepthStencilAttachment = nullptr;
    }

    // Define subpass dependencies
    std::vector<vk::SubpassDependency> dependencies;
    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask = vk::AccessFlags();
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    dependencies.push_back(dependency);

    // Create render pass
    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VulkanRenderPass vRenderPass;

    try {
        vRenderPass.renderPass = device.createRenderPass(renderPassInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create render pass: ") + e.what());
    }

    vRenderPass.desc = desc;
    vRenderPass.refCount = 1;

    RenderPassHandle handle{hashKey};
    renderPasses[handle] = vRenderPass;

    return handle;
}

FrameBufferHandle VulkanRenderer::CreateFrameBufferInternal(const FrameBufferDesc& desc) {
    // Hash the FrameBufferDesc to use as a key
    uint64_t hashKey = hashDesc(desc);

    // Check if framebuffer already exists
    auto it = frameBuffers.find(FrameBufferHandle{hashKey});
    if (it != frameBuffers.end()) {
        // Increment refCount and return existing framebuffer
        it->second.refCount++;
        return it->first;
    }

    std::vector<vk::ImageView> attachments;
    for (const auto& colorTarget : desc.color_targets) {
        auto textureIt = textures.find(colorTarget);
        if (textureIt == textures.end()) {
            throw std::runtime_error("Invalid ColorTargetHandle in FrameBufferDesc.");
        }
        attachments.push_back(textureIt->second.imageView);
    }

    if (desc.depth_target.id != 0) {
        auto depthIt = textures.find(desc.depth_target);
        if (depthIt == textures.end()) {
            throw std::runtime_error("Invalid DepthTargetHandle in FrameBufferDesc.");
        }
        attachments.push_back(depthIt->second.imageView);
    }

    // Retrieve the appropriate render pass based on desc
    RenderPassDesc renderPassDesc;

    // #FIXME 생각해 볼 문제..
    RenderPassHandle renderPassHandle = CreateRenderPassInternal(renderPassDesc);
    auto renderPassIt = renderPasses.find(renderPassHandle);
    if (renderPassIt == renderPasses.end()) {
        throw std::runtime_error("RenderPassHandle not found for FrameBufferDesc.");
    }
    vk::FramebufferCreateInfo framebufferInfo{};
    framebufferInfo.renderPass = renderPassIt->second.renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = desc.width;
    framebufferInfo.height = desc.height;
    framebufferInfo.layers = 1;

    VulkanFrameBuffer vFrameBuffer;

    try {
        vFrameBuffer.framebuffer = device.createFramebuffer(framebufferInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create framebuffer: ") + e.what());
    }

    vFrameBuffer.renderPassHandle = renderPassHandle;
    vFrameBuffer.desc = desc;
    vFrameBuffer.refCount = 1;

    FrameBufferHandle handle{hashKey};
    frameBuffers[handle] = vFrameBuffer;

    return handle;
}

BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc& desc) {
    VulkanBuffer vBuffer;

    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = desc.size;
    bufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;  // Adjust based on desc.usage

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
    vBuffer.memoryUsage = desc.memory_usage;
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

    vBuffer.buffer = vk::Buffer(buffer);
    vBuffer.allocation = allocation;
    vBuffer.size = desc.size;
    vBuffer.usage = bufferInfo.usage;

    BufferHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        buffers[handle] = vBuffer;
    }

    return handle;
}

void VulkanRenderer::UpdateBuffer(BufferHandle handle, const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = buffers.find(handle);
    if (it == buffers.end()) {
        throw std::runtime_error("Invalid BufferHandle provided to UpdateBuffer.");
    }

    VulkanBuffer& vBuffer = it->second;

    void* mappedData;
    vmaMapMemory(allocator, vBuffer.allocation, &mappedData);
    memcpy(mappedData, data, size);
    vmaUnmapMemory(allocator, vBuffer.allocation);
}

void VulkanRenderer::BindBuffer(BufferHandle handle, uint32_t bind_point, uint32_t dynamic_offset) {
    // Binding logic depends on pipeline and descriptor sets
    // This needs to be implemented based on descriptor set layout
}

TextureHandle VulkanRenderer::CreateTexture(const TextureDesc& desc) {
    VulkanTexture vTexture;

    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = (desc.type == TextureType::k2D)   ? vk::ImageType::e2D
                          : (desc.type == TextureType::k3D) ? vk::ImageType::e3D
                                                            : vk::ImageType::e2D;
    imageInfo.extent.width = desc.width;
    imageInfo.extent.height = desc.height;
    imageInfo.extent.depth = desc.depth;
    imageInfo.mipLevels = desc.mip_levels;
    imageInfo.arrayLayers = desc.array_layers;
    imageInfo.format = MapFormat(desc.format);
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

    vTexture.image = vk::Image(image);
    vTexture.allocation = allocation;
    vTexture.format = imageInfo.format;
    vTexture.extent = imageInfo.extent;
    vTexture.mipLevels = imageInfo.mipLevels;
    vTexture.arrayLayers = imageInfo.arrayLayers;
    vTexture.usage = desc.usage;

    // Create image view
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = vTexture.image;
    viewInfo.viewType = (desc.type == TextureType::k2D)   ? vk::ImageViewType::e2D
                        : (desc.type == TextureType::k3D) ? vk::ImageViewType::e3D
                                                          : vk::ImageViewType::eCube;
    viewInfo.format = vTexture.format;
    viewInfo.subresourceRange.aspectMask =
        (desc.usage & TextureUsage::kDepthStencil) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = vTexture.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = vTexture.arrayLayers;

    try {
        vTexture.imageView = device.createImageView(viewInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create image view: ") + e.what());
    }

    TextureHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        textures[handle] = vTexture;
    }

    return handle;
}

void VulkanRenderer::BindTexture(TextureHandle handle, uint32_t bind_point) {
    // Placeholder: Implement descriptor set binding logic here
    // Descriptor sets are managed internally; this method should update the appropriate descriptor sets
}

SamplerHandle VulkanRenderer::CreateSampler(const SamplerDesc& desc) {
    VulkanSampler vSampler;
    vSampler.desc = desc;  // Store sampler description

    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = static_cast<vk::Filter>(desc.mag_filter);
    samplerInfo.minFilter = static_cast<vk::Filter>(desc.min_filter);
    samplerInfo.addressModeU = static_cast<vk::SamplerAddressMode>(desc.address_mode_u);
    samplerInfo.addressModeV = static_cast<vk::SamplerAddressMode>(desc.address_mode_v);
    samplerInfo.addressModeW = static_cast<vk::SamplerAddressMode>(desc.address_mode_w);
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;  // Example value
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

    try {
        vSampler.sampler = device.createSampler(samplerInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create sampler: ") + e.what());
    }

    SamplerHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        samplers[handle] = vSampler;
    }

    return handle;
}

void VulkanRenderer::BindSampler(SamplerHandle handle, uint32_t bind_point) {
    // Placeholder: Implement descriptor set binding logic here
    // Descriptor sets are managed internally; this method should update the appropriate descriptor sets
}

ShaderHandle VulkanRenderer::CreateShader(const ShaderDesc& desc) {
    VulkanShader vShader;
    vShader.desc = desc;  // Store shader description

    // Load shader code from file (SPIR-V binary)
    std::ifstream file(desc.file_path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file.");
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    vShader.shaderModule = CreateShaderModule(buffer);

    ShaderHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        shaders[handle] = vShader;
    }

    return handle;
}

vk::ShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char>& code) {
    vk::ShaderModuleCreateInfo createInfo;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    try {
        return device.createShaderModule(createInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create shader module: ") + e.what());
    }
}

PipelineHandle VulkanRenderer::CreatePipeline(const PipelineDesc& desc) {
    VulkanPipeline vPipeline;
    vPipeline.desc = desc;  // Store pipeline description

    // Create shader stages
    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

    // Vertex Shader Stage
    if (desc.vertex_shader.id != 0) {
        auto vertShaderIt = shaders.find(desc.vertex_shader);
        if (vertShaderIt == shaders.end()) {
            throw std::runtime_error("Invalid VertexShaderHandle provided to CreatePipeline.");
        }

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
        vertShaderStageInfo.module = vertShaderIt->second.shaderModule;
        vertShaderStageInfo.pName = "main";
        shaderStages.push_back(vertShaderStageInfo);
    }

    // Fragment Shader Stage
    if (desc.fragment_shader.id != 0) {
        auto fragShaderIt = shaders.find(desc.fragment_shader);
        if (fragShaderIt == shaders.end()) {
            throw std::runtime_error("Invalid FragmentShaderHandle provided to CreatePipeline.");
        }

        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
        fragShaderStageInfo.module = fragShaderIt->second.shaderModule;
        fragShaderStageInfo.pName = "main";
        shaderStages.push_back(fragShaderStageInfo);
    }

    // Vertex Input
    std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
    std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;

    for (const auto& attr : desc.vertex_layout_desc.attributes) {
        vk::VertexInputAttributeDescription attribute{};
        attribute.location = attr.location;
        attribute.binding = 0;                                    // Assuming single binding for simplicity
        attribute.format = static_cast<vk::Format>(attr.format);  // Ensure correct mapping
        attribute.offset = attr.offset;
        attributeDescriptions.push_back(attribute);
    }

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input Assembly
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and Scissor
    vk::Viewport viewport{};
    viewport.x = desc.viewport.x;
    viewport.y = desc.viewport.y;
    viewport.width = desc.viewport.width;
    viewport.height = desc.viewport.height;
    viewport.minDepth = desc.viewport.min_depth;
    viewport.maxDepth = desc.viewport.max_depth;

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{desc.scissor.offset_x, desc.scissor.offset_y};
    scissor.extent = vk::Extent2D{desc.scissor.width, desc.scissor.height};

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = desc.rasterization.depth_clamp_enable;
    rasterizer.rasterizerDiscardEnable = desc.rasterization.rasterizer_discard_enable;
    rasterizer.polygonMode = MapFormat(desc.rasterization.polygon_mode);
    rasterizer.lineWidth = 1.0f;  // #TODO antialsing line
    rasterizer.cullMode = MapFormat(desc.rasterization.cull_mode);
    rasterizer.frontFace = MapFormat(desc.rasterization.front_face);
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Color Blending
    std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
    for (const auto& blendState : desc.color_blends) {
        vk::PipelineColorBlendAttachmentState colorBlend{};
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

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = vk::LogicOp::eCopy;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // Pipeline Layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = 0;  // Descriptor sets will be managed internally
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    try {
        vPipeline.layout = device.createPipelineLayout(pipelineLayoutInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create pipeline layout: ") + e.what());
    }

    // Pipeline Creation
    vk::GraphicsPipelineCreateInfo pipelineInfo{};
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
    pipelineInfo.layout = vPipeline.layout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr;

    try {
        vPipeline.pipeline = device.createGraphicsPipeline(nullptr, pipelineInfo).value;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create graphics pipeline: ") + e.what());
    }

    PipelineHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        pipelines[handle] = vPipeline;
    }

    return handle;
}

PipelineHandle VulkanRenderer::CreatePipeline(const ComputePipelineDesc& desc) {
    VulkanPipeline vPipeline;
    // Note: For compute pipelines, PipelineDesc and VulkanPipeline structs might need to differentiate
    vPipeline.desc = PipelineDesc();  // Initialize appropriately

    // Create shader stage
    auto computeShaderIt = shaders.find(desc.compute_shader);
    if (computeShaderIt == shaders.end()) {
        throw std::runtime_error("Invalid ComputeShaderHandle provided to CreatePipeline.");
    }

    vk::PipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    shaderStageInfo.module = computeShaderIt->second.shaderModule;
    shaderStageInfo.pName = "main";

    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages = {shaderStageInfo};

    // Pipeline Layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = 0;  // Descriptor sets will be managed internally
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    try {
        vPipeline.layout = device.createPipelineLayout(pipelineLayoutInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create compute pipeline layout: ") + e.what());
    }

    // Compute Pipeline Create Info
    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = vPipeline.layout;
    pipelineInfo.basePipelineHandle = nullptr;

    try {
        vPipeline.pipeline = device.createComputePipeline(nullptr, pipelineInfo).value;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create compute pipeline: ") + e.what());
    }

    PipelineHandle handle;
    handle.id = GenerateUniqueID();
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        pipelines[handle] = vPipeline;
    }

    return handle;
}

void VulkanRenderer::BindPipeline(PipelineHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = pipelines.find(handle);
    if (it != pipelines.end()) {
        currentPipeline = it->second.pipeline;
        // Determine bind point based on pipeline type
        // For simplicity, assuming graphics pipeline
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, it->second.pipeline);
    } else {
        throw std::runtime_error("Invalid PipelineHandle provided to BindPipeline.");
    }
}

void VulkanRenderer::DispatchCompute(uint32_t group_x, uint32_t group_y, uint32_t group_z) {
    commandBuffer.dispatch(group_x, group_y, group_z);
}

void VulkanRenderer::BeginPass(const RenderPassDesc& desc) {
    // This implementation assumes that the current swapchain is set externally
    // Alternatively, modify to accept a swapchain handle
    // For this example, assume single render pass setup

    // Define clear values based on RenderPassDesc
    std::vector<vk::ClearValue> clearValues;
    for (const auto& color : desc.clear_colors) {
        clearValues.emplace_back(
            vk::ClearValue(vk::ClearColorValue(std::array<float, 4>{color[0], color[1], color[2], color[3]})));
    }
    if (desc.clear_depth) {
        vk::ClearDepthStencilValue depthClear = {};
        depthClear.depth = desc.clear_depth_value;
        depthClear.stencil = desc.clear_stencil_value;
        clearValues.emplace_back(vk::ClearValue(depthClear));
    }

    // Retrieve the current swapchain's framebuffer
    // For simplicity, assume that the Render method sets up the current framebuffer

    // This requires associating the render pass with a specific framebuffer during Render
    // Modify the Render method accordingly

    // Example:
    // currentCommandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
}

void VulkanRenderer::EndPass() { commandBuffer.endRenderPass(); }

void VulkanRenderer::Render(const SwapChainHandle& handle, std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = swapChains.find(handle);
    if (it == swapChains.end()) {
        throw std::runtime_error("Invalid SwapChainHandle provided to Render.");
    }

    VulkanSwapChain& scData = it->second;

    // Handle synchronization
    size_t frame = scData.currentFrame;
    vk::Semaphore imageAvailableSemaphore = scData.imageAvailableSemaphores[frame];
    vk::Semaphore renderFinishedSemaphore = scData.renderFinishedSemaphores[frame];
    vk::Fence inFlightFence = scData.inFlightFences[frame];

    // Wait for the previous frame
    device.waitForFences(inFlightFence, VK_TRUE, UINT64_MAX);

    // Reset the fence
    device.resetFences(inFlightFence);

    // Acquire image from swapchain
    uint32_t imageIndex;
    vk::Result result =
        device.acquireNextImageKHR(scData.swapchain, UINT64_MAX, imageAvailableSemaphore, nullptr, &imageIndex);
    if (result == vk::Result::eErrorOutOfDateKHR) {
        throw std::runtime_error("Swapchain is out of date.");
    } else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("Failed to acquire swapchain image.");
    }

    // Begin command buffer
    commandBuffer.reset({});
    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    try {
        commandBuffer.begin(beginInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to begin command buffer: ") + e.what());
    }

    // Begin render pass using the cached RenderPassHandle and FrameBufferHandle
    VulkanRenderPass& vRenderPass = renderPasses[scData.renderPassHandle];
    VulkanFrameBuffer& vFrameBuffer = frameBuffers[scData.frameBufferHandle];

    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = vRenderPass.renderPass;
    renderPassInfo.framebuffer = vFrameBuffer.framebuffer;
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = scData.extent;

    // Define clear values based on RenderPassDesc
    RenderPassDesc passDesc = vRenderPass.desc;
    std::vector<vk::ClearValue> clearValues;
    for (const auto& color : passDesc.clear_colors) {
        vk::ClearColorValue clearColor =
            vk::ClearColorValue(std::array<float, 4>{color[0], color[1], color[2], color[3]});
        clearValues.emplace_back(clearColor);
    }
    if (passDesc.clear_depth) {
        vk::ClearDepthStencilValue depthClear = {};
        depthClear.depth = 1.0f;
        depthClear.stencil = 0;
        clearValues.emplace_back(depthClear);
    }

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    try {
        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to begin render pass: ") + e.what());
    }

    // Execute user-defined rendering commands
    callback();

    // End render pass
    try {
        commandBuffer.endRenderPass();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to end render pass: ") + e.what());
    }

    // End command buffer
    try {
        commandBuffer.end();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to end command buffer: ") + e.what());
    }

    // Submit command buffer
    vk::SubmitInfo submitInfo{};
    vk::Semaphore waitSemaphores[] = {imageAvailableSemaphore};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vk::Semaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    try {
        graphicsQueue.submit(submitInfo, inFlightFence);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to submit command buffer: ") + e.what());
    }

    // Present the image
    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &scData.swapchain;
    presentInfo.pImageIndices = &imageIndex;

    try {
        vk::Result presentResult = graphicsQueue.presentKHR(presentInfo);
        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR) {
            throw std::runtime_error("Swapchain is out of date or suboptimal.");
        } else if (presentResult != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to present swapchain image.");
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to present swapchain image: ") + e.what());
    }

    // Advance to the next frame
    scData.currentFrame = (scData.currentFrame + 1) % scData.inFlightFences.size();
}
void VulkanRenderer::DrawIndexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                                 int32_t vertex_offset, uint32_t first_instance) {
    commandBuffer.drawIndexed(index_count, instance_count, first_index, vertex_offset, first_instance);
}

bool VulkanRenderer::ReloadShader(ShaderHandle handle, const ShaderDesc& new_desc) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = shaders.find(handle);
    if (it == shaders.end())
        return false;

    // Destroy old shader module
    device.destroyShaderModule(it->second.shaderModule);

    // Load new shader
    std::ifstream file(new_desc.file_path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    it->second.shaderModule = CreateShaderModule(buffer);
    it->second.desc = new_desc;

    return true;
}

void VulkanRenderer::ReleaseResource(SwapChainHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = swapChains.find(handle);
    if (it != swapChains.end()) {
        VulkanSwapChain& scData = it->second;
        if (scData.swapchain) {
            device.destroySwapchainKHR(scData.swapchain);
        }
        ReleaseResource(scData.frameBufferHandle);
        ReleaseResource(scData.renderPassHandle);

        for (auto& imageView : scData.imageViews) {
            device.destroyImageView(imageView);
        }
        // Cleanup synchronization primitives
        for (auto& semaphore : scData.imageAvailableSemaphores) {
            device.destroySemaphore(semaphore);
        }
        for (auto& semaphore : scData.renderFinishedSemaphores) {
            device.destroySemaphore(semaphore);
        }
        for (auto& fence : scData.inFlightFences) {
            device.destroyFence(fence);
        }
        swapChains.erase(it);
    }
}

void VulkanRenderer::ReleaseResource(TextureHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = textures.find(handle);
    if (it != textures.end()) {
        // Decrement ref count
        if (--it->second.refCount == 0) {
            device.destroyImageView(it->second.imageView);
            vmaDestroyImage(allocator, static_cast<VkImage>(it->second.image), it->second.allocation);
            textures.erase(it);
        }
    }
}

void VulkanRenderer::ReleaseResource(SamplerHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = samplers.find(handle);
    if (it != samplers.end()) {
        // Decrement ref count
        if (--it->second.refCount == 0) {
            device.destroySampler(it->second.sampler);
            samplers.erase(it);
        }
    }
}

void VulkanRenderer::ReleaseResource(PipelineHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = pipelines.find(handle);
    if (it != pipelines.end()) {
        // Decrement ref count
        if (--it->second.refCount == 0) {
            device.destroyPipeline(it->second.pipeline);
            device.destroyPipelineLayout(it->second.layout);
            pipelines.erase(it);
        }
    }
}

void VulkanRenderer::ReleaseResource(ShaderHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = shaders.find(handle);
    if (it != shaders.end()) {
        // Decrement ref count
        if (--it->second.refCount == 0) {
            device.destroyShaderModule(it->second.shaderModule);
            shaders.erase(it);
        }
    }
}

void VulkanRenderer::ReleaseResource(const RenderPassHandle& handle) {}

void VulkanRenderer::ReleaseResource(const FrameBufferHandle& handle) {}

// Descriptor Set Management
vk::DescriptorSetLayout VulkanRenderer::CreateDescriptorSetLayout(
    const std::vector<DescriptorSetLayoutInfo>& bindings) {
    std::lock_guard<std::mutex> lock(descriptorMutex);

    std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;
    for (const auto& bindingInfo : bindings) {
        layoutBindings.push_back(bindingInfo.binding);
    }

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    try {
        return device.createDescriptorSetLayout(layoutInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create descriptor set layout: ") + e.what());
    }
}

vk::DescriptorSet VulkanRenderer::AllocateDescriptorSet(vk::DescriptorSetLayout layout) {
    std::lock_guard<std::mutex> lock(descriptorMutex);

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    try {
        return device.allocateDescriptorSets(allocInfo).front();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to allocate descriptor set: ") + e.what());
    }
}

void VulkanRenderer::UpdateDescriptorSet(vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type,
                                         const vk::DescriptorImageInfo& imageInfo) {
    vk::WriteDescriptorSet descriptorWrite{};
    descriptorWrite.dstSet = set;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = type;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    device.updateDescriptorSets(descriptorWrite, nullptr);
}

void VulkanRenderer::UpdateDescriptorSet(vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type,
                                         const vk::DescriptorBufferInfo& bufferInfo) {
    vk::WriteDescriptorSet descriptorWrite{};
    descriptorWrite.dstSet = set;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = type;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    device.updateDescriptorSets(descriptorWrite, nullptr);
}

void VulkanRenderer::CreateCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.queueFamilyIndex = graphicsQueueFamily;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

    try {
        commandPool = device.createCommandPool(poolInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create command pool: ") + e.what());
    }
}

void VulkanRenderer::AllocateCommandBuffer() {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    try {
        auto commandBuffers = device.allocateCommandBuffers(allocInfo);
        if (commandBuffers.empty()) {
            throw std::runtime_error("Failed to allocate command buffer.");
        }
        commandBuffer = commandBuffers[0];
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to allocate command buffer: ") + e.what());
    }
}

void VulkanRenderer::CreateDescriptorPool() {
    std::lock_guard<std::mutex> lock(descriptorMutex);

    std::vector<vk::DescriptorPoolSize> poolSizes = {
        {vk::DescriptorType::eUniformBuffer, 100}, {vk::DescriptorType::eCombinedImageSampler, 100}
        // Add more pool sizes as needed
    };

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 100;  // Adjust based on application needs

    try {
        descriptorPool = device.createDescriptorPool(poolInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create descriptor pool: ") + e.what());
    }
}

void VulkanRenderer::SetupSynchronization(VulkanSwapChain& scData) {
    scData.imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    scData.renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    scData.inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    scData.currentFrame = 0;

    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;  // Initially signaled

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        try {
            scData.imageAvailableSemaphores[i] = device.createSemaphore(semaphoreInfo);
            scData.renderFinishedSemaphores[i] = device.createSemaphore(semaphoreInfo);
            scData.inFlightFences[i] = device.createFence(fenceInfo);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create synchronization primitives: ") + e.what());
        }
    }
}

void VulkanRenderer::CleanupSynchronization(VulkanSwapChain& scData) {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        device.destroySemaphore(scData.imageAvailableSemaphores[i]);
        device.destroySemaphore(scData.renderFinishedSemaphores[i]);
        device.destroyFence(scData.inFlightFences[i]);
    }
    scData.imageAvailableSemaphores.clear();
    scData.renderFinishedSemaphores.clear();
    scData.inFlightFences.clear();
    scData.currentFrame = 0;
}

void VulkanRenderer::TransitionImageLayout(vk::Image image, vk::Format format, vk::ImageLayout oldLayout,
                                           vk::ImageLayout newLayout) {
    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    commandBuffer.begin(beginInfo);

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint) {
            barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
        }
    } else {
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    }

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
               newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else if (oldLayout == vk::ImageLayout::eUndefined &&
               newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask =
            vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
    } else {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    commandBuffer.pipelineBarrier(sourceStage, destinationStage, {},  // dependency flags
                                  0, nullptr,                         // memory barriers
                                  0, nullptr,                         // buffer memory barriers
                                  1, &barrier                         // image memory barriers
    );

    commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    vk::Fence fence = device.createFence(fenceInfo);

    graphicsQueue.submit(submitInfo, fence);
    device.waitForFences(fence, VK_TRUE, UINT64_MAX);
    device.destroyFence(fence);
}

void VulkanRenderer::InsertImageMemoryBarrier(vk::CommandBuffer& cmdBuffer, vk::Image image, vk::Format format,
                                              vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                              vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage) {
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint) {
            barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
        }
    } else {
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    }

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
               newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    } else if (oldLayout == vk::ImageLayout::eUndefined &&
               newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask =
            vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    } else {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    cmdBuffer.pipelineBarrier(srcStage, dstStage, {},  // dependency flags
                              0, nullptr,              // memory barriers
                              0, nullptr,              // buffer memory barriers
                              1, &barrier              // image memory barriers
    );
}

bool VulkanRenderer::CheckValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};

    for (const char* layerName : validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

std::vector<const char*> VulkanRenderer::GetRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;

    // Platform-specific extensions
    std::vector<const char*> extensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

#ifdef _WIN32
    // Win32 requires VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
    // Example for Xlib; adjust based on your windowing system
    extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#elif defined(__ANDROID__)
    extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(__APPLE__)
    extensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#endif

    // Enable validation layers if available
    if (CheckValidationLayerSupport()) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

vk::Format VulkanRenderer::MapFormat(Format format) {
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
Format VulkanRenderer::MapFormat(vk::Format vk_format) {
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
vk::PolygonMode VulkanRenderer::MapFormat(PolygonMode mode) {
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
vk::CullModeFlags VulkanRenderer::MapFormat(CullMode mode) {
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
vk::FrontFace VulkanRenderer::MapFormat(FrontFace face) {
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
