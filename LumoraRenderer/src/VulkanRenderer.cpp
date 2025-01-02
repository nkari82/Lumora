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
    std::vector<vk::Framebuffer> framebuffers;
    // Add other swapchain-specific data if needed (e.g., framebuffers)
};

// Descriptor Set Management Structures
struct DescriptorSetLayoutInfo {
    vk::DescriptorSetLayoutBinding binding;
    vk::DescriptorType type;
    vk::ShaderStageFlags stageFlags;
};

struct DescriptorPoolInfo {
    uint32_t maxSets;
    std::vector<vk::DescriptorPoolSize> poolSizes;
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

    // Debug messenger
    vk::DebugUtilsMessengerEXT debugMessenger;

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

    // Descriptor Set Management
    vk::DescriptorPool descriptorPool;
    std::mutex descriptorMutex;

    // Handle to index mapping
    std::mutex resourceMutex;

    // Current command buffer
    vk::CommandBuffer currentCommandBuffer;

    // Synchronization Primitives
    std::unordered_map<uint64_t, std::vector<vk::Semaphore>> imageAvailableSemaphores;
    std::unordered_map<uint64_t, std::vector<vk::Semaphore>> renderFinishedSemaphores;
    std::unordered_map<uint64_t, std::vector<vk::Fence>> inFlightFences;
    std::unordered_map<uint64_t, size_t> currentFrame;

    const int MAX_FRAMES_IN_FLIGHT = 2;

    // Internal methods
    void InitVulkan(const char* app_name);
    void CleanupVulkan();
    void CreateInstance(const char* app_name);
    void SetupDebugMessenger();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSurface(const SwapChainDesc& desc);
    SwapChainData CreateSwapChainInternal(const SwapChainDesc& desc);
    void CreateImageViews(SwapChainData& scData);
    void CreateFramebuffers(SwapChainData& scData);
    void CreateRenderPass(const RenderPassDesc& desc);
    void CreateCommandPool();
    void AllocateCommandBuffer();
    void SetupSynchronization(const SwapChainHandle& handle);
    void CleanupSynchronization(const SwapChainHandle& handle);

    // Descriptor Set Management Methods
    vk::DescriptorSetLayout CreateDescriptorSetLayout(const std::vector<DescriptorSetLayoutInfo>& bindings);
    vk::DescriptorSet AllocateDescriptorSet(vk::DescriptorSetLayout layout);
    void UpdateDescriptorSet(vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type,
                             const vk::DescriptorImageInfo& imageInfo);
    void UpdateDescriptorSet(vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type,
                             const vk::DescriptorBufferInfo& bufferInfo);

    // Helper methods
    vk::ShaderModule CreateShaderModule(const std::string& code);

    bool CheckValidationLayerSupport();
    std::vector<const char*> GetRequiredExtensions();

    Format FromVulkanFormat(vk::Format vk_format);

    vk::Format ToVulkanFormat(Format format);
    vk::Format MapFormat(Format format) { return ToVulkanFormat(format); }
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
    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator.");
    }

    // Create Command Pool
    CreateCommandPool();

    // Allocate Command Buffer
    AllocateCommandBuffer();

    // Create Render Pass with default settings
    RenderPassDesc defaultPassDesc;
    CreateRenderPass(defaultPassDesc);
}

void VulkanRenderer::CleanupVulkan() {
    std::lock_guard<std::mutex> lock(resourceMutex);

    // Destroy synchronization primitives
    for (auto& [id, semaphores] : imageAvailableSemaphores) {
        for (auto& semaphore : semaphores) {
            device.destroySemaphore(semaphore);
        }
    }
    imageAvailableSemaphores.clear();

    for (auto& [id, semaphores] : renderFinishedSemaphores) {
        for (auto& semaphore : semaphores) {
            device.destroySemaphore(semaphore);
        }
    }
    renderFinishedSemaphores.clear();

    for (auto& [id, fences] : inFlightFences) {
        for (auto& fence : fences) {
            device.destroyFence(fence);
        }
    }
    inFlightFences.clear();
    currentFrame.clear();

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
        for (auto& framebuffer : scData.framebuffers) {
            device.destroyFramebuffer(framebuffer);
        }
        for (auto& imageView : scData.imageViews) {
            device.destroyImageView(imageView);
        }
    }
    swapChains.clear();

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
    SwapChainData scData = CreateSwapChainInternal(desc);

    // Create framebuffers for this swapchain
    CreateFramebuffers(scData);

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
    createInfo.pLayer = static_cast<id<CAMetalLayer>>(desc.window_handle.cocoa.view);

    VkSurfaceKHR rawSurface;
    if (vkCreateMetalSurfaceEXT(static_cast<VkInstance>(instance), &createInfo, nullptr, &rawSurface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Metal surface.");
    }
    surface = vk::SurfaceKHR(rawSurface);
#else
    throw std::runtime_error("Unsupported platform for surface creation.");
#endif
}

//  struct SwapChainDesc {
//     void* window_handle = nullptr;
//     int32_t width = 1280;
//     int32_t height = 720;
//     Format format; // unused
//     int32_t buffer_count = 2;
//     bool vsync = true; // unused
//  };
SwapChainData VulkanRenderer::CreateSwapChainInternal(const SwapChainDesc& desc) {
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
    bool sameQueueFamily = true;  // Assuming same family for simplicity

    if (sameQueueFamily) {
        vk::SharingMode sharingMode = vk::SharingMode::eExclusive;
        uint32_t queueFamilyIndices[] = {graphicsQueueFamily};

        vk::SwapchainCreateInfoKHR createInfo;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        scData.imageFormat = ToVulkanFormat(desc.format);
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

        try {
            scData.swapchain = device.createSwapchainKHR(createInfo);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create swapchain: ") + e.what());
        }
    } else {
        // Handle different queue families if needed
        throw std::runtime_error("Different queue families for graphics and present not supported in this example.");
    }

    // Retrieve swapchain images
    scData.images = device.getSwapchainImagesKHR(scData.swapchain);

    // Create image views
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

        try {
            scData.imageViews[i] = device.createImageView(viewInfo);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create image view: ") + e.what());
        }
    }
}

// Automatically create framebuffers for each swapchain image.
// Retrieve from RenderPassDesc or SwapChainData
// #FIXME 내부적으로 자동관리 객체는 RenderPassData로 함.
void VulkanRenderer::CreateFramebuffers(SwapChainData& scData) {
    scData.framebuffers.resize(scData.imageViews.size());

    for (size_t i = 0; i < scData.imageViews.size(); ++i) {
        std::vector<vk::ImageView> attachments;

        // Add color attachments #FIXME  in CreateFramebuffers
        for (const auto& colorTarget : /* Retrieve from RenderPassDesc or SwapChainData */) {
            attachments.push_back(imageViews[colorTarget.id]);
        }

#if 0
        std::vector<vk::ImageView> attachments = { scData.imageViews[i] }
#endif

        // Add depth attachment if present
        // Assuming a single depth attachment for simplicity
        // Add code to include depth attachment if needed

        vk::FramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = scData.extent.width;
        framebufferInfo.height = scData.extent.height;
        framebufferInfo.layers = 1;

        try {
            scData.framebuffers[i] = device.createFramebuffer(framebufferInfo);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create framebuffer: ") + e.what());
        }
    }
}

BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc& desc) {
    vk::BufferCreateInfo bufferInfo;
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
    if (it == buffers.end()) {
        throw std::runtime_error("Invalid BufferHandle provided to UpdateBuffer.");
    }

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
    viewInfo.subresourceRange.aspectMask =
        (desc.usage & TextureUsage::kDepthStencil) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.mip_levels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc.array_layers;

    vk::ImageView imageView;
    try {
        imageView = device.createImageView(viewInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create image view: ") + e.what());
    }

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

    vk::Sampler sampler;
    try {
        sampler = device.createSampler(samplerInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create sampler: ") + e.what());
    }

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
    // Load shader code from file (SPIR-V binary)
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

    try {
        return device.createShaderModule(createInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create shader module: ") + e.what());
    }
}

PipelineHandle VulkanRenderer::CreatePipeline(const PipelineDesc& desc) {
    // Create shader stages
    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

    // Vertex Shader Stage
    if (desc.vertex_shader.id != 0) {
        auto vertShaderIt = shaders.find(desc.vertex_shader.id);
        if (vertShaderIt == shaders.end()) {
            throw std::runtime_error("Invalid VertexShaderHandle provided to CreatePipeline.");
        }

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo = {};
        vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
        vertShaderStageInfo.module = vertShaderIt->second;
        vertShaderStageInfo.pName = "main";
        shaderStages.push_back(vertShaderStageInfo);
    }

    // Fragment Shader Stage
    if (desc.fragment_shader.id != 0) {
        auto fragShaderIt = shaders.find(desc.fragment_shader.id);
        if (fragShaderIt == shaders.end()) {
            throw std::runtime_error("Invalid FragmentShaderHandle provided to CreatePipeline.");
        }

        vk::PipelineShaderStageCreateInfo fragShaderStageInfo = {};
        fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
        fragShaderStageInfo.module = fragShaderIt->second;
        fragShaderStageInfo.pName = "main";
        shaderStages.push_back(fragShaderStageInfo);
    }

    // Vertex Input
    std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
    std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;

    for (const auto& attr : desc.vertex_layout_desc.attributes) {
        vk::VertexInputAttributeDescription attribute = {};
        attribute.location = attr.location;
        attribute.binding = 0;                                    // Assuming single binding for simplicity
        attribute.format = static_cast<vk::Format>(attr.format);  // Ensure correct mapping
        attribute.offset = attr.offset;
        attributeDescriptions.push_back(attribute);
    }

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input Assembly
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and Scissor
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
    rasterizer.polygonMode = ToVulkanPolygonMode(desc.rasterization.polygon_mode);
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = ToVulkanCullMode(desc.rasterization.cull_mode);
    rasterizer.frontFace = ToVulkanFrontFace(desc.rasterization.front_face);
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Color Blending
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

    // Pipeline Layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.setLayoutCount = 0;  // Descriptor sets will be managed internally later
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    try {
        pipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create pipeline layout: ") + e.what());
    }

    // Pipeline Creation
    vk::GraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr;  // Implement if using depth #FIXME depthStencilState
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;  // Implement if using dynamic states #FIXME dynamic states
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr;

    try {
        vk::Pipeline graphicsPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo).value;

        // Store the pipeline
        PipelineHandle handle;
        handle.id = GenerateUniqueID();
        pipelines[handle.id] = graphicsPipeline;

        return handle;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create graphics pipeline: ") + e.what());
    }
}

PipelineHandle VulkanRenderer::CreatePipeline(const ComputePipelineDesc& desc) {
    // Create shader stage
    auto computeShaderIt = shaders.find(desc.compute_shader.id);
    if (computeShaderIt == shaders.end()) {
        throw std::runtime_error("Invalid ComputeShaderHandle provided to CreatePipeline.");
    }

    vk::PipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    shaderStageInfo.module = computeShaderIt->second;
    shaderStageInfo.pName = "main";

    // Pipeline Layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.setLayoutCount = 0;  // Descriptor sets will be managed internally later
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    try {
        vk::PipelineLayout computePipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);

        // Compute Pipeline Create Info
        vk::ComputePipelineCreateInfo pipelineInfo = {};
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = computePipelineLayout;
        pipelineInfo.basePipelineHandle = nullptr;

        // Create Compute Pipeline
        vk::Pipeline computePipeline = device.createComputePipeline(nullptr, pipelineInfo).value;

        // Store the pipeline
        PipelineHandle handle;
        handle.id = GenerateUniqueID();
        pipelines[handle.id] = computePipeline;

        // Optionally, store the computePipelineLayout if needed

        return handle;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create compute pipeline: ") + e.what());
    }
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

void VulkanRenderer::EndPass() { currentCommandBuffer.endRenderPass(); }

void VulkanRenderer::Render(const SwapChainHandle& handle, std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    auto it = swapChains.find(handle.id);
    if (it == swapChains.end()) {
        throw std::runtime_error("Invalid SwapChainHandle provided to Render.");
    }

    SwapChainData& scData = it->second;

    // Handle synchronization
    size_t frame = currentFrame[handle.id];
    vk::Semaphore imageAvailableSemaphore = imageAvailableSemaphores[handle.id][frame];
    vk::Semaphore renderFinishedSemaphore = renderFinishedSemaphores[handle.id][frame];
    vk::Fence inFlightFence = inFlightFences[handle.id][frame];

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
    currentCommandBuffer.reset({});
    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    try {
        currentCommandBuffer.begin(beginInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to begin command buffer: ") + e.what());
    }

    // Begin render pass
    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = scData.framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = scData.extent;

    // Define clear values based on RenderPassDesc
    std::vector<vk::ClearValue> clearValues;
    for (const auto& color : desc.clear_colors) {  // #FIXEME SwapChain내부에 default renderdesc 생성.
        vk::ClearColorValue clearColor =
            vk::ClearColorValue(std::array<float, 4>{color[0], color[1], color[2], color[3]});
        clearValues.emplace_back(clearColor);
    }
    if (desc.clear_depth) {
        vk::ClearDepthStencilValue depthClear = {};
        depthClear.depth = desc.clear_depth_value;
        depthClear.stencil = desc.clear_stencil_value;
        clearValues.emplace_back(depthClear);
    }

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    try {
        currentCommandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to begin render pass: ") + e.what());
    }

    // Execute user-defined rendering commands
    callback();

    // End render pass
    try {
        currentCommandBuffer.endRenderPass();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to end render pass: ") + e.what());
    }

    // End command buffer
    try {
        currentCommandBuffer.end();
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
    submitInfo.pCommandBuffers = &currentCommandBuffer;
    vk::Semaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    try {
        graphicsQueue.submit(submitInfo, inFlightFences[handle.id][frame]);
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
    currentFrame[handle.id] = (currentFrame[handle.id] + 1) % MAX_FRAMES_IN_FLIGHT;
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
        for (auto& framebuffer : scData.framebuffers) {
            device.destroyFramebuffer(framebuffer);
        }
        for (auto& imageView : scData.imageViews) {
            device.destroyImageView(imageView);
        }
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

void VulkanRenderer::CreateRenderPass(const RenderPassDesc& desc) {
    // Determine number of attachments based on desc.color_targets and depth_target
    size_t attachmentCount = desc.color_targets.size();
    bool hasDepth = desc.depth_target.id != 0;  // Assuming 0 is invalid

    if (hasDepth) {
        attachmentCount += 1;
    }

    std::vector<vk::AttachmentDescription> attachments(attachmentCount);
    std::vector<vk::AttachmentReference> colorAttachmentRefs(desc.color_targets.size());
    std::vector<vk::AttachmentReference> depthAttachmentRef(1);

    // Setup color attachments
    for (size_t i = 0; i < desc.color_targets.size(); ++i) {
        const auto& colorTarget = desc.color_targets[i];
        const auto& attachmentOpt = desc.color_attachment_options[i];

        attachments[i].format =
            images[colorTarget.id].getFormat();  // #FIXME TextureData getFormat이 없데 Assuming image format
        // maps correctly
        attachments[i].samples = vk::SampleCountFlagBits::e1;
        attachments[i].loadOp = (attachmentOpt.load_op == AttachmentLoadOp::kClear)  ? vk::AttachmentLoadOp::eClear
                                : (attachmentOpt.load_op == AttachmentLoadOp::kLoad) ? vk::AttachmentLoadOp::eLoad
                                                                                     : vk::AttachmentLoadOp::eDontCare;
        attachments[i].storeOp = (attachmentOpt.store_op == AttachmentStoreOp::kStore)
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
        const auto& depthOpt = desc.depth_attachment_options;

        size_t depthIndex = desc.color_targets.size();

        attachments[depthIndex].format = vk::Format::eD32Sfloat;  // Example format, map appropriately
        attachments[depthIndex].samples = vk::SampleCountFlagBits::e1;
        attachments[depthIndex].loadOp = (depthOpt.load_op == AttachmentLoadOp::kClear) ? vk::AttachmentLoadOp::eClear
                                         : (depthOpt.load_op == AttachmentLoadOp::kLoad)
                                             ? vk::AttachmentLoadOp::eLoad
                                             : vk::AttachmentLoadOp::eDontCare;
        attachments[depthIndex].storeOp = (depthOpt.store_op == AttachmentStoreOp::kStore)
                                              ? vk::AttachmentStoreOp::eStore
                                              : vk::AttachmentStoreOp::eDontCare;
        attachments[depthIndex].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachments[depthIndex].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachments[depthIndex].initialLayout = vk::ImageLayout::eUndefined;
        attachments[depthIndex].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        depthAttachmentRef[0].attachment = static_cast<uint32_t>(depthIndex);
        depthAttachmentRef[0].layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    }

    // Define subpasses
    vk::SubpassDescription subpass = {};
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

    vk::SubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask = vk::AccessFlags();
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    dependencies.push_back(dependency);

    // Create render pass
    vk::RenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    try {
        renderPass = device.createRenderPass(renderPassInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create render pass: ") + e.what());
    }
}

void VulkanRenderer::CreateCommandPool() {
    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

    try {
        commandPool = device.createCommandPool(poolInfo);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create command pool: ") + e.what());
    }
}

void VulkanRenderer::AllocateCommandBuffer() {
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.commandPool = commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    try {
        auto commandBuffers = device.allocateCommandBuffers(allocInfo);
        if (commandBuffers.empty()) {
            throw std::runtime_error("Failed to allocate command buffer.");
        }
        currentCommandBuffer = commandBuffers[0];
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to allocate command buffer: ") + e.what());
    }
}

void VulkanRenderer::SetupSynchronization(const SwapChainHandle& handle) {
    // Initialize synchronization primitives for the swapchain
    imageAvailableSemaphores[handle.id].resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores[handle.id].resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences[handle.id].resize(MAX_FRAMES_IN_FLIGHT);
    currentFrame[handle.id] = 0;

    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;  // Initially signaled

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        try {
            imageAvailableSemaphores[handle.id][i] = device.createSemaphore(semaphoreInfo);
            renderFinishedSemaphores[handle.id][i] = device.createSemaphore(semaphoreInfo);
            inFlightFences[handle.id][i] = device.createFence(fenceInfo);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create synchronization primitives: ") + e.what());
        }
    }
}

void VulkanRenderer::CleanupSynchronization(const SwapChainHandle& handle) {
    // Destroy synchronization primitives for the swapchain
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        device.destroySemaphore(imageAvailableSemaphores[handle.id][i]);
        device.destroySemaphore(renderFinishedSemaphores[handle.id][i]);
        device.destroyFence(inFlightFences[handle.id][i]);
    }
    imageAvailableSemaphores.erase(handle.id);
    renderFinishedSemaphores.erase(handle.id);
    inFlightFences.erase(handle.id);
    currentFrame.erase(handle.id);
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
