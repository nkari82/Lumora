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

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#define VMA_IMPLEMENTATION
#include <Lumora/IRenderer.h>
#include <vk_mem_alloc.h>
#include <xxhash.h>

// For debug messenger
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                             VkDebugUtilsMessageTypeFlagsEXT message_type,
                                             const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
                                             void* p_user_data) {
    std::cerr << "Validation Layer: " << p_callback_data->pMessage << std::endl;
    return VK_FALSE;
}

namespace lumora {

// Constants
const int kMaxFramesInFlight = 2;

// Internal Resource Handle
struct RenderPassHandle : ResourceHandle {};

// Internal
struct RenderPassDesc {
    std::vector<Format> color_formats;  // MRT를 위한 컬러 타겟 리스트
    Format depth_format;                // Depth 타겟 (optional)

    // 클리어 옵션
    std::vector<std::array<float, 4>> clear_colors;  // 각 컬러 타겟에 대한 클리어 색상
    bool clear_depth = true;                         // 깊이 클리어 여부
    float clear_depth_value = 1.0f;                  // 깊이 클리어 값
    uint32_t clear_stencil_value = 0;                // 스텐실 클리어 값

    // Attachment 옵션
    std::vector<AttachmentOptions> color_attachment_options;  // 각 컬러 타겟의 옵션
    AttachmentOptions depth_attachment_options;               // 깊이 타겟의 옵션

    // 서브패스
    std::vector<SubpassDesc> subpasses;  // RenderPass 내의 서브패스 리스트
};

struct HandleHash {
    std::size_t operator()(const ResourceHandle& handle) const { return static_cast<std::size_t>(handle.id); }
};

// Helper function to generate unique IDs for handles
static uint64_t GenerateUniqueID() {
    static uint64_t current_id = 1;
    return current_id++;
}

// Resource Structs
struct VulkanRef {
    uint32_t ref_count = 1;
};

struct VulkanBuffer : VulkanRef {
    vk::Buffer buffer;
    VmaAllocation allocation;
    vk::DeviceSize size;
    vk::BufferUsageFlags usage;
    MemoryUsage memory_usage;
};

enum class TextureCreationType { kRegular, kSwapChain };

struct VulkanTexture : VulkanRef {
    TextureDesc desc;
    vk::Image image;
    VmaAllocation allocation;
    vk::ImageView image_view;
    vk::Format format;
    vk::Extent3D extent;
    uint32_t mip_levels;
    uint32_t array_layers;
    TextureUsage usage;
    SamplerHandle sampler_handle;
    MemoryUsage memory_usage;
    TextureCreationType creation_type = TextureCreationType::kRegular;  // New member
};

struct VulkanSampler : VulkanRef {
    SamplerDesc desc;  // To store sampler configuration
    vk::Sampler sampler;
};

struct VulkanShader : VulkanRef {
    ShaderDesc desc;  // To store shader metadata
    vk::ShaderModule shader_module;
};

struct VulkanPipeline : VulkanRef {
    PipelineDesc desc;  // To store pipeline configuration
    std::unordered_map<uint64_t, vk::Pipeline> pipelines;
    vk::PipelineLayout layout;
};

// New Structs for Framebuffer and Render Pass
struct VulkanFrameBuffer : VulkanRef {
    FrameBufferDesc desc;                       // To store framebuffer description
    std::vector<vk::Framebuffer> framebuffers;  // 스왑체인 이미지별 프레임버퍼
    RenderPassHandle renderpass_handle;
};

struct VulkanRenderPass : VulkanRef {
    RenderPassDesc desc;
    vk::RenderPass renderpass;
    uint64_t desc_hash;
};

struct VulkanSwapChain : VulkanRef {
    SwapChainDesc desc;
    vk::SwapchainKHR swapchain;
    vk::SurfaceKHR surface;  // Each swapchain's Surface
    vk::Format color_format;
    vk::Format depth_format;

    // Command Pool
    vk::CommandPool command_pool;
    std::vector<vk::CommandBuffer> command_buffers;

    // Synchronization primitives
    std::vector<vk::Semaphore> image_available_semaphores;
    std::vector<vk::Semaphore> render_finished_semaphores;
    std::vector<vk::Fence> in_flight_fences;
    size_t current_frame;
};

struct DescriptorSet {
    vk::DescriptorSet descriptor_set;
    std::vector<vk::DescriptorBufferInfo> buffer_infos;
    std::vector<vk::DescriptorImageInfo> image_infos;
};

class VulkanRenderer : public IRenderer {
   public:
    VulkanRenderer() {
        // Constructor
        hash_state_ = XXH64_createState();
    }

    ~VulkanRenderer() override {
        XXH64_freeState(hash_state_);
        Close();
    }

    void Open(const char* app_name, const WindowHandle& wh) override { InitVulkan(app_name, wh); }

    void Close() override { CleanupVulkan(); }

    // public
    SwapChainHandle CreateSwapChain(const SwapChainDesc& desc) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);

        VulkanSwapChain swapchain_data;
        swapchain_data.desc = desc;

        swapchain_data.surface = CreateSurface(desc.window_handle);

        auto surface_formats = physical_device_.getSurfaceFormatsKHR(swapchain_data.surface);
        vk::SurfaceFormatKHR chosen_format = ChooseSurfaceFormat(surface_formats);

        auto present_modes = physical_device_.getSurfacePresentModesKHR(swapchain_data.surface);
        vk::PresentModeKHR chosen_present_mode = ChoosePresentMode(present_modes);

        auto capabilities = physical_device_.getSurfaceCapabilitiesKHR(swapchain_data.surface);
        vk::Extent2D chosen_extent = ChooseExtent(capabilities, desc.width, desc.height);

        uint32_t image_count = desc.buffer_count;
        if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
            image_count = capabilities.maxImageCount;
        }

        vk::SwapchainCreateInfoKHR swapchain_info{};
        swapchain_info.sType = vk::StructureType::eSwapchainCreateInfoKHR;
        swapchain_info.surface = swapchain_data.surface;
        swapchain_info.minImageCount = image_count;
        swapchain_info.imageFormat = chosen_format.format;
        swapchain_info.imageColorSpace = chosen_format.colorSpace;
        swapchain_info.imageExtent = chosen_extent;
        swapchain_info.imageArrayLayers = 1;
        swapchain_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

        // Queue Family Handling
        if (graphics_queue_family_ != present_queue_family_) {
            swapchain_info.imageSharingMode = vk::SharingMode::eConcurrent;
            uint32_t queue_family_indices[] = {graphics_queue_family_, present_queue_family_};
            swapchain_info.queueFamilyIndexCount = 2;
            swapchain_info.pQueueFamilyIndices = queue_family_indices;
        } else {
            swapchain_info.imageSharingMode = vk::SharingMode::eExclusive;
        }

        swapchain_info.preTransform = capabilities.currentTransform;
        swapchain_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapchain_info.presentMode = chosen_present_mode;
        swapchain_info.clipped = VK_TRUE;
        swapchain_info.oldSwapchain = nullptr;

        try {
            swapchain_data.swapchain = device_.createSwapchainKHR(swapchain_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create swap chain: ") + e.what());
        }

        swapchain_data.command_pool = CreateCommandPool();

        vk::CommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = vk::StructureType::eCommandBufferAllocateInfo;
        alloc_info.commandPool = swapchain_data.command_pool;
        alloc_info.level = vk::CommandBufferLevel::ePrimary;
        alloc_info.commandBufferCount = kMaxFramesInFlight;  // 예: 프레임당 하나의 커맨드 버퍼

        try {
            swapchain_data.command_buffers = device_.allocateCommandBuffers(alloc_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to allocate command buffers: ") + e.what());
        }

        SetupSynchronization(swapchain_data);

        SwapChainHandle handle;
        handle.id = GenerateUniqueID();
        swapchains_.emplace(handle, swapchain_data);

        return handle;
    }

    BufferHandle CreateBuffer(const BufferDesc& desc) override {
        VulkanBuffer vbuffer;

        vk::BufferCreateInfo buffer_info{};
        buffer_info.size = desc.size;
        buffer_info.usage = vk::BufferUsageFlagBits::eVertexBuffer;  // Adjust based on desc.usage

        if (desc.usage & BufferUsage::kVertex)
            buffer_info.usage |= vk::BufferUsageFlagBits::eVertexBuffer;
        if (desc.usage & BufferUsage::kIndex)
            buffer_info.usage |= vk::BufferUsageFlagBits::eIndexBuffer;
        if (desc.usage & BufferUsage::kUniform)
            buffer_info.usage |= vk::BufferUsageFlagBits::eUniformBuffer;
        if (desc.usage & BufferUsage::kStorage)
            buffer_info.usage |= vk::BufferUsageFlagBits::eStorageBuffer;
        if (desc.usage & BufferUsage::kIndirect)
            buffer_info.usage |= vk::BufferUsageFlagBits::eIndirectBuffer;

        VmaAllocationCreateInfo alloc_info = {};
        vbuffer.memory_usage = desc.memory_usage;
        switch (desc.memory_usage) {
            case MemoryUsage::kGpuOnly:
                alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
                break;
            case MemoryUsage::kCpuToGpu:
                alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
                break;
            case MemoryUsage::kAuto:
            default:
                alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
                break;
        }

        VkBuffer buffer;
        VmaAllocation allocation;
        if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&buffer_info), &alloc_info, &buffer,
                            &allocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create buffer.");
        }

        vbuffer.buffer = vk::Buffer(buffer);
        vbuffer.allocation = allocation;
        vbuffer.size = desc.size;
        vbuffer.usage = buffer_info.usage;

        BufferHandle handle;
        handle.id = GenerateUniqueID();
        {
            std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
            buffers_.emplace(handle, vbuffer);
        }

        return handle;
    }

    void UpdateBuffer(const BufferHandle& handle, const void* data, size_t size) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto it = buffers_.find(handle);
        if (it == buffers_.end()) {
            throw std::runtime_error("Invalid BufferHandle provided to UpdateBuffer.");
        }

        VulkanBuffer& vbuffer = it->second;

        void* mapped_data;
        vmaMapMemory(allocator_, vbuffer.allocation, &mapped_data);
        std::memcpy(mapped_data, data, size);
        vmaUnmapMemory(allocator_, vbuffer.allocation);
    }

    void BindBuffer(const BufferHandle& handle, uint32_t bind_point, uint32_t dynamic_offset = 0) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto buffer_it = buffers_.find(handle);
        if (buffer_it == buffers_.end()) {
            throw std::runtime_error("Invalid BufferHandle provided to BindBuffer.");
        }

        // Allocate or retrieve a descriptor set
        DescriptorSet ds = AllocateDescriptorSet();
        UpdateDescriptorSet(handle, TextureHandle{0}, ds);  // Assuming no texture binding here

        // Bind descriptor set
        command_buffer_.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout_, 0, ds.descriptor_set,
                                           nullptr);
    }

    TextureHandle CreateTexture(const TextureDesc& desc) override {
        VulkanTexture vtexture;

        vk::ImageCreateInfo image_info{};
        image_info.imageType = (desc.type == TextureType::k2D)   ? vk::ImageType::e2D
                               : (desc.type == TextureType::k3D) ? vk::ImageType::e3D
                                                                 : vk::ImageType::e2D;  // Default to 2D
        image_info.extent.width = desc.width;
        image_info.extent.height = desc.height;
        image_info.extent.depth = desc.depth;
        image_info.mipLevels = desc.mip_levels;
        image_info.arrayLayers = desc.array_layers;
        image_info.format = Convert(desc.format);
        image_info.tiling = vk::ImageTiling::eOptimal;
        image_info.initialLayout = vk::ImageLayout::eUndefined;
        image_info.usage = Convert(desc.usage);

        VmaAllocationCreateInfo alloc_info = {};
        vtexture.memory_usage = desc.memory_usage;
        switch (desc.memory_usage) {
            case MemoryUsage::kGpuOnly:
                alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
                break;
            case MemoryUsage::kCpuToGpu:
                alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
                break;
            case MemoryUsage::kAuto:
            default:
                alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
                break;
        }

        VkImage image;
        VmaAllocation allocation;
        if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&image_info), &alloc_info, &image,
                           &allocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image.");
        }

        vtexture.image = vk::Image(image);
        vtexture.allocation = allocation;
        vtexture.format = image_info.format;
        vtexture.extent = image_info.extent;
        vtexture.mip_levels = image_info.mipLevels;
        vtexture.array_layers = image_info.arrayLayers;
        vtexture.creation_type = TextureCreationType::kRegular;  // Default creation_type

        // Determine aspect mask
        vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlagBits::eColor;
        if ((desc.usage & TextureUsage::kDepthStencil) != TextureUsage::kNone) {
            aspect_mask = vk::ImageAspectFlagBits::eDepth;
            if (image_info.format == vk::Format::eD24UnormS8Uint || image_info.format == vk::Format::eD32SfloatS8Uint) {
                aspect_mask |= vk::ImageAspectFlagBits::eStencil;
            }
        }

        // Create image view using the shared CreateView method
        vtexture.image_view = CreateView(vtexture.image, vtexture.format, aspect_mask);

        TextureHandle handle;
        handle.id = GenerateUniqueID();
        {
            std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
            textures_.emplace(handle, vtexture);
        }

        return handle;
    }

    void BindTexture(const TextureHandle& handle, uint32_t bind_point) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto texture_it = textures_.find(handle);
        if (texture_it == textures_.end()) {
            throw std::runtime_error("Invalid TextureHandle provided to BindTexture.");
        }

        // Allocate or retrieve a descriptor set
        DescriptorSet ds = AllocateDescriptorSet();
        UpdateDescriptorSet(BufferHandle{0}, handle, ds);  // Assuming no buffer binding here

        // Bind descriptor set
        command_buffer_.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout_, 0, ds.descriptor_set,
                                           nullptr);
    }

    SamplerHandle CreateSampler(const SamplerDesc& desc) override {
        VulkanSampler vsampler;
        vsampler.desc = desc;  // Store sampler description

        vk::SamplerCreateInfo sampler_info{};
        sampler_info.magFilter = static_cast<vk::Filter>(desc.mag_filter);
        sampler_info.minFilter = static_cast<vk::Filter>(desc.min_filter);
        sampler_info.addressModeU = static_cast<vk::SamplerAddressMode>(desc.address_mode_u);
        sampler_info.addressModeV = static_cast<vk::SamplerAddressMode>(desc.address_mode_v);
        sampler_info.addressModeW = static_cast<vk::SamplerAddressMode>(desc.address_mode_w);
        sampler_info.anisotropyEnable = VK_TRUE;
        sampler_info.maxAnisotropy = 16.0f;  // Example value
        sampler_info.borderColor = vk::BorderColor::eIntOpaqueBlack;
        sampler_info.unnormalizedCoordinates = VK_FALSE;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.compareOp = vk::CompareOp::eAlways;
        sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;

        try {
            vsampler.sampler = device_.createSampler(sampler_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create sampler: ") + e.what());
        }

        SamplerHandle handle;
        handle.id = GenerateUniqueID();
        {
            std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
            samplers_.emplace(handle, vsampler);
        }

        return handle;
    }

    void BindSampler(const SamplerHandle& handle, uint32_t bind_point) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto sampler_it = samplers_.find(handle);
        if (sampler_it == samplers_.end()) {
            throw std::runtime_error("Invalid SamplerHandle provided to BindSampler.");
        }

        // For simplicity, assume sampler is already bound via descriptor sets when binding textures
        // Additional implementation may be required based on specific use cases
    }

    ShaderHandle CreateShader(const ShaderDesc& desc) override {
        VulkanShader vshader;
        vshader.desc = desc;  // Store shader description

        // Load shader code from file (SPIR-V binary)
        std::ifstream file(desc.file_path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file.");
        }

        size_t file_size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(file_size);
        file.seekg(0);
        file.read(buffer.data(), file_size);
        file.close();

        vshader.shader_module = CreateShaderModule(buffer);

        ShaderHandle handle;
        handle.id = GenerateUniqueID();
        {
            std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
            shaders_.emplace(handle, vshader);
        }

        return handle;
    }

    PipelineHandle CreatePipeline(const PipelineDesc& desc) override {
        VulkanPipeline vpipeline;
        vpipeline.desc = desc;  // Store pipeline description

        // Create shader stages
        std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;

        // Vertex Shader Stage
        if (desc.vertex_shader.id != 0) {
            auto vert_shader_it = shaders_.find(desc.vertex_shader);
            if (vert_shader_it == shaders_.end()) {
                throw std::runtime_error("Invalid VertexShaderHandle provided to CreatePipeline.");
            }

            vk::PipelineShaderStageCreateInfo vert_shader_stage_info{};
            vert_shader_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
            vert_shader_stage_info.module = vert_shader_it->second.shader_module;
            vert_shader_stage_info.pName = "main";
            shader_stages.push_back(vert_shader_stage_info);
        }

        // Fragment Shader Stage
        if (desc.fragment_shader.id != 0) {
            auto frag_shader_it = shaders_.find(desc.fragment_shader);
            if (frag_shader_it == shaders_.end()) {
                throw std::runtime_error("Invalid FragmentShaderHandle provided to CreatePipeline.");
            }

            vk::PipelineShaderStageCreateInfo frag_shader_stage_info{};
            frag_shader_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
            frag_shader_stage_info.module = frag_shader_it->second.shader_module;
            frag_shader_stage_info.pName = "main";
            shader_stages.push_back(frag_shader_stage_info);
        }

        // Vertex Input
        std::vector<vk::VertexInputBindingDescription> binding_descriptions;
        std::vector<vk::VertexInputAttributeDescription> attribute_descriptions;

        for (const auto& attr : desc.vertex_layout_desc.attributes) {
            vk::VertexInputAttributeDescription attribute{};
            attribute.location = attr.location;
            attribute.binding = 0;                                    // Assuming single binding for simplicity
            attribute.format = static_cast<vk::Format>(attr.format);  // Ensure correct mapping
            attribute.offset = attr.offset;
            attribute_descriptions.push_back(attribute);
        }

        vk::PipelineVertexInputStateCreateInfo vertex_input_info{};
        vertex_input_info.vertexBindingDescriptionCount = static_cast<uint32_t>(binding_descriptions.size());
        vertex_input_info.pVertexBindingDescriptions = binding_descriptions.data();
        vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
        vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

        // Input Assembly
        vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
        input_assembly.primitiveRestartEnable = VK_FALSE;

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

        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;

        // Rasterizer
        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = desc.rasterization.depth_clamp_enable;
        rasterizer.rasterizerDiscardEnable = desc.rasterization.rasterizer_discard_enable;
        rasterizer.polygonMode = Convert(desc.rasterization.polygon_mode);
        rasterizer.lineWidth = 1.0f;  // #TODO anti-aliasing line
        rasterizer.cullMode = Convert(desc.rasterization.cull_mode);
        rasterizer.frontFace = Convert(desc.rasterization.front_face);
        rasterizer.depthBiasEnable = VK_FALSE;

        // Multisampling
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Color Blending
        std::vector<vk::PipelineColorBlendAttachmentState> color_blend_attachments;
        for (const auto& blend_state : desc.color_blends) {
            vk::PipelineColorBlendAttachmentState color_blend{};
            color_blend.blendEnable = blend_state.blend_enable;
            color_blend.srcColorBlendFactor = static_cast<vk::BlendFactor>(blend_state.src_color_blend_factor);
            color_blend.dstColorBlendFactor = static_cast<vk::BlendFactor>(blend_state.dst_color_blend_factor);
            color_blend.colorBlendOp = static_cast<vk::BlendOp>(blend_state.color_blend_op);
            color_blend.srcAlphaBlendFactor = static_cast<vk::BlendFactor>(blend_state.src_alpha_blend_factor);
            color_blend.dstAlphaBlendFactor = static_cast<vk::BlendFactor>(blend_state.dst_alpha_blend_factor);
            color_blend.alphaBlendOp = static_cast<vk::BlendOp>(blend_state.alpha_blend_op);
            color_blend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                         vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            color_blend_attachments.push_back(color_blend);
        }

        vk::PipelineColorBlendStateCreateInfo color_blending{};
        color_blending.logicOpEnable = VK_FALSE;
        color_blending.logicOp = vk::LogicOp::eCopy;
        color_blending.attachmentCount = static_cast<uint32_t>(color_blend_attachments.size());
        color_blending.pAttachments = color_blend_attachments.data();
        color_blending.blendConstants[0] = 0.0f;
        color_blending.blendConstants[1] = 0.0f;
        color_blending.blendConstants[2] = 0.0f;
        color_blending.blendConstants[3] = 0.0f;

        // Pipeline Layout
        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 0;  // Descriptor sets will be managed internally
        pipeline_layout_info.pSetLayouts = nullptr;
        pipeline_layout_info.pushConstantRangeCount = 0;
        pipeline_layout_info.pPushConstantRanges = nullptr;

        try {
            vpipeline.layout = device_.createPipelineLayout(pipeline_layout_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create pipeline layout: ") + e.what());
        }

        // Pipeline Creation moved to BindPipeline
#if 0
    vk::GraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = nullptr;  // Implement if using depth
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = nullptr;  // Implement if using dynamic states
    pipeline_info.layout = vpipeline.layout;
    pipeline_info.renderPass = render_pass_;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = nullptr;

    try {
      vpipeline.pipeline = device_.createGraphicsPipeline(nullptr, pipeline_info).value;
    } catch (const std::exception& e) {
      throw std::runtime_error(std::string("Failed to create graphics pipeline: ") + e.what());
    }
#endif
        PipelineHandle handle;
        handle.id = GenerateUniqueID();
        {
            std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
            pipelines_.emplace(handle, vpipeline);
        }

        return handle;
    }

    PipelineHandle CreatePipeline(const ComputePipelineDesc& desc) override {
        VulkanPipeline vpipeline;
        // Note: For compute pipelines, PipelineDesc and VulkanPipeline structs might need to differentiate
        vpipeline.desc = PipelineDesc();  // Initialize appropriately

        // Create shader stage
        auto compute_shader_it = shaders_.find(desc.compute_shader);
        if (compute_shader_it == shaders_.end()) {
            throw std::runtime_error("Invalid ComputeShaderHandle provided to CreatePipeline.");
        }

        vk::PipelineShaderStageCreateInfo shader_stage_info{};
        shader_stage_info.stage = vk::ShaderStageFlagBits::eCompute;
        shader_stage_info.module = compute_shader_it->second.shader_module;
        shader_stage_info.pName = "main";

        std::vector<vk::PipelineShaderStageCreateInfo> shader_stages = {shader_stage_info};

        // Pipeline Layout
        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 0;  // Descriptor sets will be managed internally
        pipeline_layout_info.pSetLayouts = nullptr;
        pipeline_layout_info.pushConstantRangeCount = 0;
        pipeline_layout_info.pPushConstantRanges = nullptr;

        try {
            vpipeline.layout = device_.createPipelineLayout(pipeline_layout_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create compute pipeline layout: ") + e.what());
        }

        // Compute Pipeline Create Info
        vk::ComputePipelineCreateInfo pipeline_info{};
        pipeline_info.stage = shader_stage_info;
        pipeline_info.layout = vpipeline.layout;
        pipeline_info.basePipelineHandle = nullptr;

#if 0
    try {
      vpipeline.pipelines = device_.createComputePipeline(nullptr, pipeline_info).value;
    } catch (const std::exception& e) {
      throw std::runtime_error(std::string("Failed to create compute pipeline: ") + e.what());
    }
#endif
        PipelineHandle handle;
        handle.id = GenerateUniqueID();
        {
            std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
            pipelines_.emplace(handle, vpipeline);
        }

        return handle;
    }

    // BeginPass시 RenderDesc로 CreateFrameBuffer를 생성하고 CreateRenderPass를 생성한다.
    FrameBufferHandle CreateFrameBuffer(const FrameBufferDesc& desc) {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);

        // Populate RenderPassDesc based on FrameBufferDesc
        RenderPassDesc render_pass_desc;
        for (const auto& texture_handle : desc.color_targets) {
            auto texture_it = textures_.find(texture_handle);
            if (texture_it == textures_.end()) {
                throw std::runtime_error("Invalid TextureHandle in FrameBufferDesc.color_targets.");
            }
            render_pass_desc.color_formats.push_back(texture_it->second.desc.format);
        }

        if (desc.depth_target.id != 0) {  // Assuming TextureHandle{0} is invalid
            auto depth_it = textures_.find(desc.depth_target);
            if (depth_it == textures_.end()) {
                throw std::runtime_error("Invalid TextureHandle in FrameBufferDesc.depth_target.");
            }
            render_pass_desc.depth_format = depth_it->second.desc.format;
        } else {
            render_pass_desc.depth_format = Format::kUnknown;
        }

        // Assign other members from FrameBufferDesc to RenderPassDesc
        render_pass_desc.clear_colors = desc.clear_colors;
        render_pass_desc.clear_depth = desc.clear_depth;
        render_pass_desc.clear_depth_value = desc.clear_depth_value;
        render_pass_desc.clear_stencil_value = desc.clear_stencil_value;
        render_pass_desc.color_attachment_options = desc.color_attachment_options;
        render_pass_desc.depth_attachment_options = desc.depth_attachment_options;
        render_pass_desc.subpasses = desc.subpasses;

        // Create or retrieve RenderPass
        RenderPassHandle renderpass_handle = CreateRenderPassInternal(render_pass_desc);

        // Create Framebuffer
        VulkanFrameBuffer vframebuffer;
        // Gather image views for attachments
        std::vector<vk::ImageView> attachments;
        for (const auto& color_handle : desc.color_targets) {
            auto texture_it = textures_.find(color_handle);
            if (texture_it == textures_.end()) {
                throw std::runtime_error("Invalid TextureHandle in FrameBufferDesc.color_targets.");
            }
            attachments.push_back(texture_it->second.image_view);
        }

        if (desc.depth_target.id != 0) {
            auto depth_it = textures_.find(desc.depth_target);
            if (depth_it == textures_.end()) {
                throw std::runtime_error("Invalid TextureHandle in FrameBufferDesc.depth_target.");
            }
            attachments.push_back(depth_it->second.image_view);
        }

        // Retrieve the RenderPass
        auto render_pass_it = render_passes_.find(renderpass_handle);
        if (render_pass_it == render_passes_.end()) {
            throw std::runtime_error("RenderPassHandle not found for FrameBufferDesc.");
        }

        // Use the width and height from FrameBufferDesc
        uint32_t width = desc.width;
        uint32_t height = desc.height;

        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.renderPass = render_pass_it->second.renderpass;
        framebuffer_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebuffer_info.pAttachments = attachments.data();
        framebuffer_info.width = width;
        framebuffer_info.height = height;
        framebuffer_info.layers = 1;

        try {
            vframebuffer.framebuffers.emplace_back(device_.createFramebuffer(framebuffer_info));
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create framebuffer: ") + e.what());
        }

        vframebuffer.renderpass_handle = renderpass_handle;
        vframebuffer.desc = desc;
        vframebuffer.ref_count = 1;

        FrameBufferHandle handle;
        handle.id = GenerateUniqueID();
        framebuffers_.emplace(handle, vframebuffer);

        return handle;
    }

    FrameBufferHandle CreateFrameBuffer(const SwapChainHandle& handle) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);

        // SwapChain 조회
        auto swapchain_it = swapchains_.find(handle);
        if (swapchain_it == swapchains_.end()) {
            throw std::runtime_error("Invalid SwapChainHandle provided to CreateFrameBuffer.");
        }

        VulkanSwapChain& sc_data = swapchains_.at(handle);

        // 스왑체인 이미지 가져오기
        std::vector<vk::Image> swapchain_images = device_.getSwapchainImagesKHR(sc_data.swapchain);

        // RenderPass 생성 (SwapChainDesc를 기반으로)
        RenderPassDesc render_pass_desc;
        render_pass_desc.color_formats = {sc_data.desc.color_format};
        render_pass_desc.depth_format = sc_data.desc.depth_format;
        render_pass_desc.clear_colors = {{0.0f, 0.0f, 0.0f, 1.0f}};
        render_pass_desc.clear_depth = sc_data.desc.depth_format != Format::kUnknown;
        render_pass_desc.clear_depth_value = 1.0f;
        render_pass_desc.clear_stencil_value = 0;
        render_pass_desc.color_attachment_options = {
            AttachmentOptions{.load_op = AttachmentLoadOp::kClear, .store_op = AttachmentStoreOp::kStore}};
        if (render_pass_desc.clear_depth) {
            render_pass_desc.depth_attachment_options =
                AttachmentOptions{.load_op = AttachmentLoadOp::kClear, .store_op = AttachmentStoreOp::kStore};
        }

        // #FIXME (If pDepthStencilAttachment is not NULL) RenderPass 생성 또는 조회
        RenderPassHandle renderpass_handle = CreateRenderPassInternal(render_pass_desc);

        // VulkanFrameBuffer 생성
        VulkanFrameBuffer vframebuffer;
        vframebuffer.renderpass_handle = renderpass_handle;

        // FrameBufferDesc 초기화 (프레임버퍼 생성에 필요 없음, 직접 생성)
        // 스왑체인 이미지별로 Framebuffer 생성
        for (const auto& image : swapchain_images) {
            // CreateView 메소드를 사용하여 이미지 뷰 생성
            vk::ImageView image_view = CreateView(image, sc_data.color_format, vk::ImageAspectFlagBits::eColor);

            // TextureHandle 생성 (kSwapChain 타입)
            TextureHandle texture_handle;
            texture_handle.id = GenerateUniqueID();

            // VulkanTexture 구조체 채우기
            VulkanTexture vtexture;
            vtexture.image = image;
            vtexture.image_view = image_view;
            vtexture.format = sc_data.color_format;
            vtexture.extent = vk::Extent3D{sc_data.desc.width, sc_data.desc.height, 1};
            vtexture.mip_levels = 1;
            vtexture.array_layers = 1;
            vtexture.usage = TextureUsage::kRenderTarget;
            vtexture.creation_type = TextureCreationType::kSwapChain;

            // textures_ 맵에 추가
            textures_.emplace(texture_handle, vtexture);

            // Framebuffer 생성 정보 설정
            std::vector<vk::ImageView> attachments = {image_view};

            // 깊이 텍스처가 필요한 경우
            TextureHandle depth_handle = TextureHandle{0};
            if (render_pass_desc.clear_depth) {
                // 깊이 텍스처 생성
                depth_handle = CreateTexture({
                    .format = sc_data.desc.depth_format,
                    .usage = TextureUsage::kDepthStencil,
                    .width = sc_data.desc.width,
                    .height = sc_data.desc.height,
                    .depth = 1,
                    .mip_levels = 1,
                    .array_layers = 1,
                    .memory_usage = MemoryUsage::kGpuOnly,  // 필요에 따라 조정
                });

                // 깊이 텍스처의 ImageView 가져오기
                auto depth_it = textures_.find(depth_handle);
                if (depth_it == textures_.end()) {
                    throw std::runtime_error("Failed to find depth texture after creation.");
                }

                // 깊이 어태치먼트 추가
                attachments.emplace_back(depth_it->second.image_view);
            }

            // FramebufferCreateInfo 설정
            vk::FramebufferCreateInfo framebuffer_info{};
            framebuffer_info.renderPass = render_passes_.at(renderpass_handle).renderpass;
            framebuffer_info.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebuffer_info.pAttachments = attachments.data();
            framebuffer_info.width = sc_data.desc.width;
            framebuffer_info.height = sc_data.desc.height;
            framebuffer_info.layers = 1;

            // Framebuffer 생성
            vk::Framebuffer framebuffer;
            try {
                framebuffer = device_.createFramebuffer(framebuffer_info);
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Failed to create framebuffer: ") + e.what());
            }

            // 생성된 Framebuffer를 VulkanFrameBuffer의 벡터에 추가
            vframebuffer.framebuffers.emplace_back(framebuffer);
        }

        FrameBufferHandle fb_handle;
        fb_handle.id = GenerateUniqueID();
        framebuffers_.emplace(fb_handle, vframebuffer);
        return fb_handle;
    }

    void BindPipeline(const PipelineHandle& handle, const uint8_t* constants, size_t size,
                      uint32_t sub_index = 0) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);

        // Retrieve VulkanPipeline
        auto pipeline_it = pipelines_.find(handle);
        if (pipeline_it == pipelines_.end()) {
            throw std::runtime_error("Invalid PipelineHandle provided to BindPipeline.");
        }

        VulkanPipeline& vpipeline = pipeline_it->second;

        // Retrieve current framebuffer's render pass hash
        // Assuming current framebuffer is tracked; otherwise, pass it as a parameter or track it globally
        // For this example, we'll assume a single swapchain/framebuffer is active
        if (swapchains_.empty()) {
            throw std::runtime_error("No active swapchain found.");
        }

        VulkanSwapChain& current_swapchain = swapchains_.begin()->second;
        VulkanRenderPass current_render_pass;  // = render_passes_[current_swapchain.renderpass_handle];

        uint64_t render_pass_hash = current_render_pass.desc_hash;
        uint32_t current_pass = current_pass_;  // Current subpass index

        // Combine render pass hash and subpass index to create a unique key
        std::hash<uint64_t> hasher;
        uint64_t combined_hash = hasher(render_pass_hash) ^ (static_cast<uint64_t>(current_pass) << 32);

        // Check if pipeline with combined_hash exists
        auto existing_pipeline_it = vpipeline.pipelines.find(combined_hash);
        if (existing_pipeline_it != vpipeline.pipelines.end()) {
            // Pipeline already exists, bind it
            command_buffer_.bindPipeline(vk::PipelineBindPoint::eGraphics, existing_pipeline_it->second);
        } else {
            // Create a new pipeline based on the stored desc
            vk::GraphicsPipelineCreateInfo pipeline_info;
            //= vpipeline.desc.ToVulkanPipelineCreateInfo();  // Assume this method exists

            // Set dynamic states or other states based on sub_index if needed
            // Modify pipeline_info based on sub_index

            vk::Pipeline new_pipeline;
            try {
                new_pipeline = device_.createGraphicsPipeline(nullptr, pipeline_info).value;
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Failed to create graphics pipeline: ") + e.what());
            }

            // Store the new pipeline in the map
            vpipeline.pipelines.emplace(combined_hash, new_pipeline);

            // Bind the new pipeline
            command_buffer_.bindPipeline(vk::PipelineBindPoint::eGraphics, new_pipeline);
        }

        // Optionally handle push constants if provided
        if (constants && size > 0) {
            // Assuming push constant ranges are defined in pipeline layout
            command_buffer_.pushConstants(
                vpipeline.layout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,  // Adjust as needed
                0,                                                                      // Offset
                size, constants);
        }
    }

    void DispatchCompute(uint32_t group_x, uint32_t group_y, uint32_t group_z) override {
        command_buffer_.dispatch(group_x, group_y, group_z);
    }

    void BeginPass(const FrameBufferHandle& handle, uint32_t image_index) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);

        auto framebuffer_it = framebuffers_.find(handle);
        if (framebuffer_it == framebuffers_.end()) {
            throw std::runtime_error("Invalid FrameBufferHandle provided to BeginPass.");
        }

        VulkanFrameBuffer& vframebuffer = framebuffer_it->second;

        // 현재 RenderPassHandle 할당 (단계 3)
        current_render_pass_handle_ = vframebuffer.renderpass_handle;

        VulkanRenderPass& vrender_pass = render_passes_.at(current_render_pass_handle_);

        // 클리어 값 설정
        std::vector<vk::ClearValue> clear_values;
        for (const auto& color : vrender_pass.desc.clear_colors) {
            vk::ClearColorValue clear_color =
                vk::ClearColorValue(std::array<float, 4>{color[0], color[1], color[2], color[3]});
            clear_values.emplace_back(clear_color);
        }
        if (vrender_pass.desc.clear_depth) {
            vk::ClearDepthStencilValue clear_depth = {};
            clear_depth.depth = vrender_pass.desc.clear_depth_value;
            clear_depth.stencil = vrender_pass.desc.clear_stencil_value;
            clear_values.emplace_back(clear_depth);
        }

        // RenderPass 시작
        vk::RenderPassBeginInfo render_pass_info{};
        render_pass_info.renderPass = vrender_pass.renderpass;
        render_pass_info.framebuffer = vframebuffer.framebuffers[image_index];
        render_pass_info.renderArea.offset = vk::Offset2D{0, 0};
        render_pass_info.renderArea.extent = vk::Extent2D{vframebuffer.desc.width, vframebuffer.desc.height};
        render_pass_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
        render_pass_info.pClearValues = clear_values.data();

        try {
            command_buffer_.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to begin render pass: ") + e.what());
        }
    }

    void EndPass() override { command_buffer_.endRenderPass(); }

    void NextPass() override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        current_pass_++;
    }

    void Render(const SwapChainHandle& handle, std::function<void(uint32_t)> callback) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto it = swapchains_.find(handle);
        if (it == swapchains_.end()) {
            throw std::runtime_error("Invalid SwapChainHandle provided to Render.");
        }

        VulkanSwapChain& sc_data = it->second;

        // Synchronization primitives
        size_t frame = sc_data.current_frame;
        vk::Semaphore image_available_semaphore = sc_data.image_available_semaphores[frame];
        vk::Semaphore render_finished_semaphore = sc_data.render_finished_semaphores[frame];
        vk::Fence in_flight_fence = sc_data.in_flight_fences[frame];

        // Wait for the previous frame to finish
        device_.waitForFences(in_flight_fence, VK_TRUE, UINT64_MAX);

        // Reset the fence for the current frame
        device_.resetFences(in_flight_fence);

        // Acquire the next image from the swapchain
        uint32_t image_index = 0;  // 현재 스왑체인 이미지
        vk::Result result = device_.acquireNextImageKHR(sc_data.swapchain, UINT64_MAX, image_available_semaphore,
                                                        nullptr, &image_index);
        if (result == vk::Result::eErrorOutOfDateKHR) {
            throw std::runtime_error("Swapchain is out of date.");
        } else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
            throw std::runtime_error("Failed to acquire swapchain image.");
        }

        command_buffer_ = sc_data.command_buffers[sc_data.current_frame];

        // Reset and begin the command buffer
        command_buffer_.reset({});
        vk::CommandBufferBeginInfo begin_info{};
        begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

        try {
            command_buffer_.begin(begin_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to begin command buffer: ") + e.what());
        }

        // 사용자 정의 렌더링 명령 실행 (콜백에서 BeginPass와 EndPass를 호출함)
        callback(image_index);

        // 커맨드 버퍼 종료
        try {
            command_buffer_.end();
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to end command buffer: ") + e.what());
        }

        // 커맨드 버퍼 제출
        vk::SubmitInfo submit_info{};
        vk::Semaphore wait_semaphores[] = {image_available_semaphore};
        vk::PipelineStageFlags wait_stages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer_;
        vk::Semaphore signal_semaphores[] = {render_finished_semaphore};
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        try {
            graphics_queue_.submit(submit_info, in_flight_fence);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to submit command buffer: ") + e.what());
        }

        // 이미지 프레젠트
        vk::PresentInfoKHR present_info{};
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &sc_data.swapchain;
        present_info.pImageIndices = &image_index;

        try {
            vk::Result present_result = graphics_queue_.presentKHR(present_info);
            if (present_result == vk::Result::eErrorOutOfDateKHR || present_result == vk::Result::eSuboptimalKHR) {
                throw std::runtime_error("Swapchain is out of date or suboptimal.");
            } else if (present_result != vk::Result::eSuccess) {
                throw std::runtime_error("Failed to present swapchain image.");
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to present swapchain image: ") + e.what());
        }

        // 다음 프레임으로 이동
        sc_data.current_frame = (sc_data.current_frame + 1) % kMaxFramesInFlight;
    }

    void DrawIndexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0,
                     int32_t vertex_offset = 0, uint32_t first_instance = 0) override {
        command_buffer_.drawIndexed(index_count, instance_count, first_index, vertex_offset, first_instance);
    }

    bool ReloadShader(const ShaderHandle& handle, const ShaderDesc& new_desc) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto it = shaders_.find(handle);
        if (it == shaders_.end())
            return false;

        // Destroy old shader module
        device_.destroyShaderModule(it->second.shader_module);

        // Load new shader
        std::ifstream file(new_desc.file_path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        size_t file_size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(file_size);
        file.seekg(0);
        file.read(buffer.data(), file_size);
        file.close();

        it->second.shader_module = CreateShaderModule(buffer);
        it->second.desc = new_desc;

        return true;
    }

    void ReleaseResource(const SwapChainHandle& handle) {
        auto it = swapchains_.find(handle);
        if (it != swapchains_.end()) {
            VulkanSwapChain& sc_data = it->second;

            // Synchronization primitives 정리
            for (auto& semaphore : sc_data.image_available_semaphores) {
                device_.destroySemaphore(semaphore);
            }
            for (auto& semaphore : sc_data.render_finished_semaphores) {
                device_.destroySemaphore(semaphore);
            }
            for (auto& fence : sc_data.in_flight_fences) {
                device_.destroyFence(fence);
            }

            // Command Pool 정리
            if (sc_data.command_pool) {
                device_.destroyCommandPool(sc_data.command_pool);
            }

            // Swapchain 정리
            if (sc_data.swapchain) {
                device_.destroySwapchainKHR(sc_data.swapchain);
            }

            // Surface 정리
            if (sc_data.surface) {
                instance_.destroySurfaceKHR(sc_data.surface);
            }

            // 스왑체인 맵에서 제거
            swapchains_.erase(it);
        }
    }

    void ReleaseResource(const TextureHandle& handle) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto it = textures_.find(handle);
        if (it != textures_.end()) {
            // Decrement ref count
            if (--it->second.ref_count == 0) {
                // Destroy image view
                device_.destroyImageView(it->second.image_view);

                if (it->second.creation_type == TextureCreationType::kRegular) {
                    // Only destroy image and allocation if not from swapchain
                    if (it->second.allocation != VK_NULL_HANDLE) {
                        vmaDestroyImage(allocator_, static_cast<VkImage>(it->second.image), it->second.allocation);
                    }
                }
                // If creation_type is kSwapChain, do not destroy the image itself

                textures_.erase(it);
            }
        }
    }

    void ReleaseResource(const SamplerHandle& handle) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto it = samplers_.find(handle);
        if (it != samplers_.end()) {
            // Decrement ref count
            if (--it->second.ref_count == 0) {
                device_.destroySampler(it->second.sampler);
                samplers_.erase(it);
            }
        }
    }

    void ReleaseResource(const PipelineHandle& handle) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto it = pipelines_.find(handle);
        if (it != pipelines_.end()) {
            // Decrement ref count
            if (--it->second.ref_count == 0) {
                // device_.destroyPipeline(it->second.pipeline);
                device_.destroyPipelineLayout(it->second.layout);
                pipelines_.erase(it);
            }
        }
    }

    void ReleaseResource(const ShaderHandle& handle) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto it = shaders_.find(handle);
        if (it != shaders_.end()) {
            // Decrement ref count
            if (--it->second.ref_count == 0) {
                device_.destroyShaderModule(it->second.shader_module);
                shaders_.erase(it);
            }
        }
    }

    void ReleaseResource(const FrameBufferHandle& handle) override {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto it = framebuffers_.find(handle);
        if (it != framebuffers_.end()) {
            if (--it->second.ref_count == 0) {
                for (auto& framebuffer : it->second.framebuffers) device_.destroyFramebuffer(framebuffer);
                ReleaseResource(it->second.renderpass_handle);
                framebuffers_.erase(it);
            }
        }
    }

   private:
    // Vulkan core components
    XXH64_state_t* hash_state_;
    vk::Instance instance_;
    vk::PhysicalDevice physical_device_;
    vk::Device device_;
    vk::RenderPass render_pass_;
    vk::CommandBuffer command_buffer_;  // current command buffer

    uint32_t graphics_queue_family_;  // Graphics Queue Family Index
    uint32_t present_queue_family_;   // Present Queue Family Index
    vk::Queue graphics_queue_;        // Graphics Queue

    // Debug messenger
    vk::DebugUtilsMessengerEXT debug_messenger_;

    // VMA Allocator
    VmaAllocator allocator_;

    // Resource maps using dedicated structs
    std::unordered_map<BufferHandle, VulkanBuffer, HandleHash> buffers_;
    std::unordered_map<TextureHandle, VulkanTexture, HandleHash> textures_;
    std::unordered_map<SamplerHandle, VulkanSampler, HandleHash> samplers_;
    std::unordered_map<ShaderHandle, VulkanShader, HandleHash> shaders_;
    std::unordered_map<PipelineHandle, VulkanPipeline, HandleHash> pipelines_;
    std::unordered_map<SwapChainHandle, VulkanSwapChain, HandleHash> swapchains_;
    std::unordered_map<FrameBufferHandle, VulkanFrameBuffer, HandleHash> framebuffers_;
    std::unordered_map<RenderPassHandle, VulkanRenderPass, HandleHash> render_passes_;

    // Handle to index mapping
    std::recursive_mutex resource_mutex_;

    // Descriptor Set Management
    vk::DescriptorPool descriptor_pool_;
    vk::DescriptorSetLayout descriptor_set_layout_;  // #TODO 내부적으로 자동 관리
    std::mutex descriptor_mutex_;

    // Current pipeline handle
    vk::Pipeline current_pipeline_;
    vk::PipelineLayout pipeline_layout_;  // #TODO 내부적으로 자동 관리
    uint32_t current_pass_ = 0;
    RenderPassHandle current_render_pass_handle_;

    // Internal methods
    void InitVulkan(const char* app_name, const WindowHandle& wh) {
        // Create Vulkan Instance
        CreateInstance(app_name);

        vk::SurfaceKHR surface = CreateSurface(wh);

        PickPhysicalDevice(surface);

        CreateLogicalDevice(surface);

        instance_.destroySurfaceKHR(surface);

        // Initialize VMA
        VmaAllocatorCreateInfo allocator_info = {};
        allocator_info.physicalDevice = static_cast<VkPhysicalDevice>(physical_device_);
        allocator_info.device = static_cast<VkDevice>(device_);
        allocator_info.instance = static_cast<VkInstance>(instance_);
        if (vmaCreateAllocator(&allocator_info, &allocator_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create VMA allocator.");
        }

        // Create Descriptor Pool
        CreateDescriptorPool();

        CreateDescriptorSetLayouts();
    }

    void CleanupVulkan() {
        device_.waitIdle();

        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        bool memory_leak = false;

        // Destroy all pipelines
        for (auto& [handle, pipeline] : pipelines_) {
            // device_.destroyPipeline(pipeline.pipeline);
            device_.destroyPipelineLayout(pipeline.layout);
            if (pipeline.ref_count != 0) {
                memory_leak = true;
                std::cerr << "Memory Leak: Pipeline ID " << handle.id << " has refCount " << pipeline.ref_count
                          << std::endl;
            }
        }
        pipelines_.clear();

        // Destroy all shader modules
        for (auto& [handle, shader] : shaders_) {
            device_.destroyShaderModule(shader.shader_module);
            if (shader.ref_count != 0) {
                memory_leak = true;
                std::cerr << "Memory Leak: Shader ID " << handle.id << " has refCount " << shader.ref_count
                          << std::endl;
            }
        }
        shaders_.clear();

        // Destroy all samplers
        for (auto& [handle, sampler] : samplers_) {
            device_.destroySampler(sampler.sampler);
            if (sampler.ref_count != 0) {
                memory_leak = true;
                std::cerr << "Memory Leak: Sampler ID " << handle.id << " has refCount " << sampler.ref_count
                          << std::endl;
            }
        }
        samplers_.clear();

        // Destroy all image views and images
        for (auto& [handle, texture] : textures_) {
            device_.destroyImageView(texture.image_view);
            if (texture.creation_type == TextureCreationType::kRegular && texture.allocation != VK_NULL_HANDLE) {
                vmaDestroyImage(allocator_, static_cast<VkImage>(texture.image), texture.allocation);
            }
            if (texture.ref_count != 0) {
                memory_leak = true;
                std::cerr << "Memory Leak: Texture ID " << handle.id << " has refCount " << texture.ref_count
                          << std::endl;
            }
        }
        textures_.clear();

        // Destroy all buffers
        for (auto& [handle, buffer] : buffers_) {
            vmaDestroyBuffer(allocator_, static_cast<VkBuffer>(buffer.buffer), buffer.allocation);
            if (buffer.ref_count != 0) {
                memory_leak = true;
                std::cerr << "Memory Leak: Buffer ID " << handle.id << " has refCount " << buffer.ref_count
                          << std::endl;
            }
        }
        buffers_.clear();

        // Destroy all framebuffers
        for (auto& [handle, framebuffer] : framebuffers_) {
            device_.destroyFramebuffer(framebuffer.framebuffers[0]);
            ReleaseResource(framebuffer.renderpass_handle);
            if (framebuffer.ref_count != 0) {
                memory_leak = true;
                std::cerr << "Memory Leak: FrameBuffer ID " << handle.id << " has refCount " << framebuffer.ref_count
                          << std::endl;
            }
        }
        framebuffers_.clear();

        // Destroy all render passes
        for (auto& [handle, render_pass_struct] : render_passes_) {
            device_.destroyRenderPass(render_pass_struct.renderpass);
            if (render_pass_struct.ref_count != 0) {
                memory_leak = true;
                std::cerr << "Memory Leak: RenderPass ID " << handle.id << " has refCount "
                          << render_pass_struct.ref_count << std::endl;
            }
        }
        render_passes_.clear();

        // Destroy all swapchains and their image views and framebuffers
        for (auto& [handle, sc_data] : swapchains_) {
            if (sc_data.swapchain) {
                device_.destroySwapchainKHR(sc_data.swapchain);
            }
            // Destroy synchronization primitives
            for (auto& semaphore : sc_data.image_available_semaphores) {
                device_.destroySemaphore(semaphore);
            }
            for (auto& semaphore : sc_data.render_finished_semaphores) {
                device_.destroySemaphore(semaphore);
            }
            for (auto& fence : sc_data.in_flight_fences) {
                device_.destroyFence(fence);
            }
        }
        swapchains_.clear();

        if (memory_leak) {
            std::cerr << "VulkanRenderer Cleanup: Memory leaks detected." << std::endl;
        }

        // Destroy Render Pass
        if (render_pass_) {
            device_.destroyRenderPass(render_pass_);
        }

        // Destroy Descriptor Pool
        if (descriptor_pool_) {
            device_.destroyDescriptorPool(descriptor_pool_);
        }

        // Destroy Debug Messenger
        if (debug_messenger_) {
            auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(static_cast<VkInstance>(instance_),
                                                                                   "vkDestroyDebugUtilsMessengerEXT");
            if (func != nullptr) {
                func(static_cast<VkInstance>(instance_), static_cast<VkDebugUtilsMessengerEXT>(debug_messenger_),
                     nullptr);
            }
        }

        // Destroy VMA allocator
        if (allocator_) {
            vmaDestroyAllocator(allocator_);
        }

        // Destroy Vulkan device
        if (device_) {
            device_.destroy();
        }

        // Destroy Vulkan instance
        if (instance_) {
            instance_.destroy();
        }
    }

    void CreateInstance(const char* app_name) {
        // Validation layers
        const std::vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"};

        if (!CheckValidationLayerSupport()) {
            throw std::runtime_error("Validation layers requested, but not available!");
        }

        // Application info
        vk::ApplicationInfo app_info{};
        app_info.pApplicationName = app_name;
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "Lumora";
        app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion = VK_API_VERSION_1_2;

        // Get required extensions
        std::vector<const char*> extensions = GetRequiredExtensions();

        vk::InstanceCreateInfo create_info{};
        create_info.pApplicationInfo = &app_info;

        // Enable extensions
        create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();

        // Enable validation layers
        create_info.enabledLayerCount = static_cast<uint32_t>(validation_layers.size());
        create_info.ppEnabledLayerNames = validation_layers.data();

        // Debug messenger create info (optional)
        vk::DebugUtilsMessengerCreateInfoEXT debug_create_info = {};
        debug_create_info.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                                            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        debug_create_info.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        debug_create_info.pfnUserCallback = DebugCallback;

        create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debug_create_info;

        // Create instance
        try {
            instance_ = vk::createInstance(create_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create Vulkan instance: ") + e.what());
        }
    }

    void SetupDebugMessenger() {
        if (!CheckValidationLayerSupport())
            return;

        vk::DebugUtilsMessengerCreateInfoEXT create_info{};
        create_info.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                                      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                      vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        create_info.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                  vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                  vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        create_info.pfnUserCallback = DebugCallback;

        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(static_cast<VkInstance>(instance_),
                                                                              "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            VkDebugUtilsMessengerEXT messenger;
            if (func(static_cast<VkInstance>(instance_),
                     reinterpret_cast<const VkDebugUtilsMessengerCreateInfoEXT*>(&create_info), nullptr,
                     &messenger) != VK_SUCCESS) {
                throw std::runtime_error("Failed to set up debug messenger!");
            }
            debug_messenger_ = vk::DebugUtilsMessengerEXT(messenger);
        } else {
            throw std::runtime_error("Could not load vkCreateDebugUtilsMessengerEXT");
        }
    }

    struct QueueFamilyIndices {
        std::vector<uint32_t> graphics_family_indices;
        uint32_t present_family = UINT32_MAX;

        bool isComplete() const { return !graphics_family_indices.empty() && present_family != UINT32_MAX; }
    };

    void PickPhysicalDevice(vk::SurfaceKHR surface) {
        auto physical_devices = instance_.enumeratePhysicalDevices();
        if (physical_devices.empty()) {
            throw std::runtime_error("Failed to find GPUs with Vulkan support.");
        }

        for (const auto& device_candidate : physical_devices) {
            if (IsDeviceSuitable(device_candidate, surface)) {
                physical_device_ = device_candidate;
                return;
            }
        }

        throw std::runtime_error("Failed to find a suitable GPU.");
    }

    bool IsDeviceSuitable(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
        // 큐 패밀리 인덱스 확인
        QueueFamilyIndices indices = FindQueueFamilies(device, surface);
        return indices.isComplete();
    }

    QueueFamilyIndices FindQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
        QueueFamilyIndices indices;
        std::vector<vk::QueueFamilyProperties> queue_families = device.getQueueFamilyProperties();

        for (uint32_t i = 0; i < queue_families.size(); i++) {
            if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                indices.graphics_family_indices.push_back(i);
            }

            if (device.getSurfaceSupportKHR(i, surface)) {
                indices.present_family = i;
            }

            if (indices.isComplete()) {
                break;
            }
        }

        return indices;
    }

    void CreateLogicalDevice(vk::SurfaceKHR surface) {
        // Find queue families for this physical device and surface
        QueueFamilyIndices indices = FindQueueFamilies(physical_device_, surface);

        if (indices.graphics_family_indices.empty() || indices.present_family == UINT32_MAX) {
            throw std::runtime_error("Failed to find required queue families.");
        }

        graphics_queue_family_ = indices.graphics_family_indices[0];
        present_queue_family_ = indices.present_family;

        // 큐 패밀리 인덱스의 유일성을 보장
        std::vector<uint32_t> unique_queue_families = {graphics_queue_family_};
        if (present_queue_family_ != present_queue_family_) {
            unique_queue_families.push_back(present_queue_family_);
        }

        std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
        float queue_priority = 1.0f;
        for (uint32_t queue_family : unique_queue_families) {
            vk::DeviceQueueCreateInfo queue_create_info{};
            queue_create_info.sType = vk::StructureType::eDeviceQueueCreateInfo;
            queue_create_info.queueFamilyIndex = queue_family;
            queue_create_info.queueCount = 1;
            queue_create_info.pQueuePriorities = &queue_priority;
            queue_create_infos.push_back(queue_create_info);
        }

        vk::PhysicalDeviceFeatures device_features{};  // 필요한 기능 활성화

        std::vector<const char*> device_extensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
            // 필요한 다른 확장들 추가
        };

        vk::DeviceCreateInfo create_info{};
        create_info.sType = vk::StructureType::eDeviceCreateInfo;
        create_info.pQueueCreateInfos = queue_create_infos.data();
        create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
        create_info.pEnabledFeatures = &device_features;
        create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
        create_info.ppEnabledExtensionNames = device_extensions.data();

        try {
            device_ = physical_device_.createDevice(create_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create logical device: ") + e.what());
        }

        // 그래픽 큐 가져오기
        graphics_queue_ = device_.getQueue(graphics_queue_family_, 0);

        // 프레젠트 큐 가져오기
        if (present_queue_family_ != graphics_queue_family_) {
            graphics_queue_ = device_.getQueue(present_queue_family_, 0);
        }
    }

    vk::SurfaceFormatKHR ChooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& available_formats) {
        for (const auto& available_format : available_formats) {
            if (available_format.format == vk::Format::eB8G8R8A8Unorm &&
                available_format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                return available_format;
            }
        }
        return available_formats[0];
    }

    vk::PresentModeKHR ChoosePresentMode(const std::vector<vk::PresentModeKHR>& available_present_modes) {
        for (const auto& available_present_mode : available_present_modes) {
            if (available_present_mode == vk::PresentModeKHR::eMailbox) {
                return available_present_mode;
            }
        }
        return vk::PresentModeKHR::eFifo;
    }

    vk::Extent2D ChooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) {
        if (capabilities.currentExtent.width != UINT32_MAX) {
            return capabilities.currentExtent;
        } else {
            vk::Extent2D actual_extent = {width, height};
            actual_extent.width =
                std::clamp(actual_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actual_extent.height = std::clamp(actual_extent.height, capabilities.minImageExtent.height,
                                              capabilities.maxImageExtent.height);
            return actual_extent;
        }
    }

    vk::SurfaceKHR CreateSurface(const WindowHandle& window_handle) {
        VkSurfaceKHR raw_surface;

#ifdef _WIN32
        HWND hwnd = static_cast<HWND>(window_handle.display);
        HINSTANCE hinstance = static_cast<HINSTANCE>(window_handle.platform);

        VkWin32SurfaceCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        create_info.hwnd = hwnd;
        create_info.hinstance = hinstance;

        if (vkCreateWin32SurfaceKHR(static_cast<VkInstance>(instance_), &create_info, nullptr, &raw_surface) !=
            VK_SUCCESS) {
            throw std::runtime_error("Failed to create Win32 surface.");
        }
#elif defined(__linux__)
        // Xlib 예시; 실제로 사용하는 windowing system에 맞게 수정 필요
        Display* display = static_cast<Display*>(window_handle.display);  // 사용자 정의
        Window window = 0;                                                // 사용자 정의

        VkXlibSurfaceCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        create_info.dpy = display;
        create_info.window = window;

        if (vkCreateXlibSurfaceKHR(static_cast<VkInstance>(instance_), &create_info, nullptr, &raw_surface) !=
            VK_SUCCESS) {
            throw std::runtime_error("Failed to create Xlib surface.");
        }
#elif defined(__ANDROID__)
        // Android 예시; 실제로 사용하는 경우에 맞게 수정 필요
        ANativeWindow* window = static_cast<ANativeWindow*>(window_handle.display);

        VkAndroidSurfaceCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        create_info.window = window;

        if (vkCreateAndroidSurfaceKHR(static_cast<VkInstance>(instance_), &create_info, nullptr, &raw_surface) !=
            VK_SUCCESS) {
            throw std::runtime_error("Failed to create Android surface.");
        }
#elif defined(__APPLE__)
        // macOS/iOS 예시; 실제로 사용하는 경우에 맞게 수정 필요
        id<CAMetalLayer> view = (__bridge id<CAMetalLayer>)(window_handle.display);

        VkMetalSurfaceCreateInfoEXT create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
        create_info.pLayer = (__bridge void*)view;

        if (vkCreateMetalSurfaceEXT(static_cast<VkInstance>(instance_), &create_info, nullptr, &raw_surface) !=
            VK_SUCCESS) {
            throw std::runtime_error("Failed to create Metal surface.");
        }
#else
        throw std::runtime_error("Unsupported platform for surface creation.");
#endif

        return vk::SurfaceKHR(raw_surface);
    }

    RenderPassHandle CreateRenderPassInternal(const RenderPassDesc& desc) {
        // Hash the RenderPassDesc to use as a key
        uint64_t hash_key = HashDesc(desc);

        // Create a unique handle
        RenderPassHandle handle{hash_key};

        // Check if render pass already exists
        auto it = render_passes_.find(handle);
        if (it != render_passes_.end()) {
            it->second.ref_count++;
            return it->first;
        }

        size_t attachment_count = desc.color_formats.size();
        bool has_depth = (desc.depth_format != Format::kUnknown);
        if (has_depth) {
            attachment_count += 1;
        }

        std::vector<vk::AttachmentDescription> attachments(attachment_count);
        std::vector<vk::AttachmentReference> color_attachment_refs(desc.color_formats.size());
        std::vector<vk::AttachmentReference> depth_attachment_ref;

        // Setup color attachments
        for (size_t i = 0; i < desc.color_formats.size(); i++) {
            attachments[i].format = Convert(desc.color_formats[i]);
            attachments[i].samples = vk::SampleCountFlagBits::e1;
            attachments[i].loadOp = (desc.color_attachment_options[i].load_op == AttachmentLoadOp::kClear)
                                        ? vk::AttachmentLoadOp::eClear
                                    : (desc.color_attachment_options[i].load_op == AttachmentLoadOp::kLoad)
                                        ? vk::AttachmentLoadOp::eLoad
                                        : vk::AttachmentLoadOp::eDontCare;
            attachments[i].storeOp = (desc.color_attachment_options[i].store_op == AttachmentStoreOp::kStore)
                                         ? vk::AttachmentStoreOp::eStore
                                         : vk::AttachmentStoreOp::eDontCare;
            attachments[i].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            attachments[i].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            attachments[i].initialLayout = vk::ImageLayout::eUndefined;
            attachments[i].finalLayout = vk::ImageLayout::ePresentSrcKHR;

            color_attachment_refs[i].attachment = static_cast<uint32_t>(i);
            color_attachment_refs[i].layout = vk::ImageLayout::eColorAttachmentOptimal;
        }

        // Setup depth attachment if present
        if (has_depth) {
            attachments[desc.color_formats.size()].format = Convert(desc.depth_format);
            attachments[desc.color_formats.size()].samples = vk::SampleCountFlagBits::e1;
            attachments[desc.color_formats.size()].loadOp =
                (desc.depth_attachment_options.load_op == AttachmentLoadOp::kClear)  ? vk::AttachmentLoadOp::eClear
                : (desc.depth_attachment_options.load_op == AttachmentLoadOp::kLoad) ? vk::AttachmentLoadOp::eLoad
                                                                                     : vk::AttachmentLoadOp::eDontCare;
            attachments[desc.color_formats.size()].storeOp =
                (desc.depth_attachment_options.store_op == AttachmentStoreOp::kStore)
                    ? vk::AttachmentStoreOp::eStore
                    : vk::AttachmentStoreOp::eDontCare;
            attachments[desc.color_formats.size()].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            attachments[desc.color_formats.size()].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            attachments[desc.color_formats.size()].initialLayout = vk::ImageLayout::eUndefined;
            attachments[desc.color_formats.size()].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

            vk::AttachmentReference depth_ref{};
            depth_ref.attachment = static_cast<uint32_t>(desc.color_formats.size());
            depth_ref.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

            depth_attachment_ref.push_back(depth_ref);
        }

        // Define subpasses
        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = static_cast<uint32_t>(color_attachment_refs.size());
        subpass.pColorAttachments = color_attachment_refs.data();
        if (has_depth) {
            subpass.pDepthStencilAttachment = &depth_attachment_ref[0];
        } else {
            subpass.pDepthStencilAttachment = nullptr;
        }

        // Define subpass dependencies (if any)
        std::vector<vk::SubpassDependency> dependencies;
        // Example dependency; adjust as needed
        vk::SubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = vk::AccessFlagBits::eNone;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        dependencies.push_back(dependency);

        // Create render pass
        vk::RenderPassCreateInfo render_pass_info{};
        render_pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        render_pass_info.pAttachments = attachments.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = static_cast<uint32_t>(dependencies.size());
        render_pass_info.pDependencies = dependencies.data();

        VulkanRenderPass vrender_pass;

        try {
            vrender_pass.renderpass = device_.createRenderPass(render_pass_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create render pass: ") + e.what());
        }

        vrender_pass.desc = desc;
        vrender_pass.ref_count = 1;
        vrender_pass.desc_hash = hash_key;  // Store the hash

        // Store the render pass
        render_passes_.emplace(handle, vrender_pass);

        return handle;
    }

    vk::ImageView CreateView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspect_mask) {
        vk::ImageViewCreateInfo view_info{};
        view_info.image = image;
        view_info.viewType = vk::ImageViewType::e2D;
        view_info.format = format;
        view_info.components.r = vk::ComponentSwizzle::eIdentity;
        view_info.components.g = vk::ComponentSwizzle::eIdentity;
        view_info.components.b = vk::ComponentSwizzle::eIdentity;
        view_info.components.a = vk::ComponentSwizzle::eIdentity;
        view_info.subresourceRange.aspectMask = aspect_mask;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        try {
            return device_.createImageView(view_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create image view: ") + e.what());
        }
    }

    vk::CommandPool CreateCommandPool() {
        vk::CommandPoolCreateInfo pool_info{};
        pool_info.sType = vk::StructureType::eCommandPoolCreateInfo;
        pool_info.queueFamilyIndex = graphics_queue_family_;
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;  // 필요한 플래그 설정

        try {
            return device_.createCommandPool(pool_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create command pool: ") + e.what());
        }
    }

    void SetupSynchronization(VulkanSwapChain& sc_data) {
        sc_data.image_available_semaphores.resize(kMaxFramesInFlight);
        sc_data.render_finished_semaphores.resize(kMaxFramesInFlight);
        sc_data.in_flight_fences.resize(kMaxFramesInFlight);
        sc_data.current_frame = 0;

        vk::SemaphoreCreateInfo semaphore_info{};
        vk::FenceCreateInfo fence_info{};
        fence_info.flags = vk::FenceCreateFlagBits::eSignaled;  // Initially signaled

        for (int i = 0; i < kMaxFramesInFlight; i++) {
            try {
                sc_data.image_available_semaphores[i] = device_.createSemaphore(semaphore_info);
                sc_data.render_finished_semaphores[i] = device_.createSemaphore(semaphore_info);
                sc_data.in_flight_fences[i] = device_.createFence(fence_info);
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Failed to create synchronization primitives: ") + e.what());
            }
        }
    }

    void CleanupSynchronization(VulkanSwapChain& sc_data) {
        for (int i = 0; i < kMaxFramesInFlight; i++) {
            device_.destroySemaphore(sc_data.image_available_semaphores[i]);
            device_.destroySemaphore(sc_data.render_finished_semaphores[i]);
            device_.destroyFence(sc_data.in_flight_fences[i]);
        }
        sc_data.image_available_semaphores.clear();
        sc_data.render_finished_semaphores.clear();
        sc_data.in_flight_fences.clear();
        sc_data.current_frame = 0;
    }

    void TransitionImageLayout(vk::CommandBuffer cmd_buffer, vk::Image image, vk::Format format,
                               vk::ImageLayout old_layout, vk::ImageLayout new_layout) {
        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;

        if (new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
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

        vk::PipelineStageFlags source_stage;
        vk::PipelineStageFlags destination_stage;

        if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eTransferDstOptimal) {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            source_stage = vk::PipelineStageFlagBits::eTopOfPipe;
            destination_stage = vk::PipelineStageFlagBits::eTransfer;
        } else if (old_layout == vk::ImageLayout::eTransferDstOptimal &&
                   new_layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            source_stage = vk::PipelineStageFlagBits::eTransfer;
            destination_stage = vk::PipelineStageFlagBits::eFragmentShader;
        } else if (old_layout == vk::ImageLayout::eUndefined &&
                   new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask =
                vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

            source_stage = vk::PipelineStageFlagBits::eTopOfPipe;
            destination_stage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        } else {
            throw std::invalid_argument("Unsupported layout transition!");
        }

        cmd_buffer.pipelineBarrier(source_stage, destination_stage, {},  // dependency flags
                                   0, nullptr,                           // memory barriers
                                   0, nullptr,                           // buffer memory barriers
                                   1, &barrier                           // image memory barriers
        );
    }

    void InsertImageMemoryBarrier(vk::CommandBuffer& cmd_buffer, vk::Image image, vk::Format format,
                                  vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                  vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage) {
        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;

        if (new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
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

        if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eTransferDstOptimal) {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        } else if (old_layout == vk::ImageLayout::eTransferDstOptimal &&
                   new_layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        } else if (old_layout == vk::ImageLayout::eUndefined &&
                   new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask =
                vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        } else {
            throw std::invalid_argument("Unsupported layout transition!");
        }

        cmd_buffer.pipelineBarrier(src_stage, dst_stage, {},  // dependency flags
                                   0, nullptr,                // memory barriers
                                   0, nullptr,                // buffer memory barriers
                                   1, &barrier                // image memory barriers
        );
    }

    bool CheckValidationLayerSupport() {
        uint32_t layer_count;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

        std::vector<VkLayerProperties> available_layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

        const std::vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"};

        for (const char* layer_name : validation_layers) {
            bool layer_found = false;

            for (const auto& layer_properties : available_layers) {
                if (std::strcmp(layer_name, layer_properties.layerName) == 0) {
                    layer_found = true;
                    break;
                }
            }

            if (!layer_found) {
                return false;
            }
        }

        return true;
    }

    std::vector<const char*> GetRequiredExtensions() {
        uint32_t glfw_extension_count = 0;
        const char** glfw_extensions;

        // Platform-specific extensions
        std::vector<const char*> extensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_KHR_SURFACE_EXTENSION_NAME};

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

    void CreateDescriptorSetLayouts() {
        // Example: Create a simple descriptor set layout with uniform buffers and sampled images
        std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            // Binding 0: Uniform Buffer
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex},
            // Binding 1: Combined Image Sampler
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eCombinedImageSampler, 1,
                                           vk::ShaderStageFlagBits::eFragment}};

        vk::DescriptorSetLayoutCreateInfo layout_info{};
        layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
        layout_info.pBindings = bindings.data();

        try {
            descriptor_set_layout_ = device_.createDescriptorSetLayout(layout_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create descriptor set layout: ") + e.what());
        }

        // Create Pipeline Layout with Descriptor Set Layout
        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &descriptor_set_layout_;

        try {
            pipeline_layout_ = device_.createPipelineLayout(pipeline_layout_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create pipeline layout: ") + e.what());
        }
    }

    void CreateDescriptorPool() {
        std::lock_guard<std::mutex> lock_(descriptor_mutex_);

        std::vector<vk::DescriptorPoolSize> pool_sizes = {
            {vk::DescriptorType::eUniformBuffer, 100}, {vk::DescriptorType::eCombinedImageSampler, 100}
            // Add more pool sizes as needed
        };

        vk::DescriptorPoolCreateInfo pool_info{};
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        pool_info.maxSets = 100;  // Adjust based on application needs

        try {
            descriptor_pool_ = device_.createDescriptorPool(pool_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create descriptor pool: ") + e.what());
        }
    }

    DescriptorSet AllocateDescriptorSet() {
        vk::DescriptorSetAllocateInfo alloc_info{};
        alloc_info.descriptorPool = descriptor_pool_;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &descriptor_set_layout_;

        vk::DescriptorSet descriptor_set;
        try {
            descriptor_set = device_.allocateDescriptorSets(alloc_info).front();
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to allocate descriptor set: ") + e.what());
        }

        DescriptorSet ds;
        ds.descriptor_set = descriptor_set;
        // descriptorSets_[descriptor_set] = ds; // #FIXME hashing

        return ds;
    }

    void UpdateDescriptorSet(BufferHandle buffer_handle, TextureHandle texture_handle, DescriptorSet& ds) {
        auto buffer_it = buffers_.find(buffer_handle);
        if (buffer_it == buffers_.end()) {
            throw std::runtime_error("Invalid BufferHandle provided to UpdateDescriptorSet.");
        }

        auto texture_it = textures_.find(texture_handle);
        if (texture_it == textures_.end()) {
            throw std::runtime_error("Invalid TextureHandle provided to UpdateDescriptorSet.");
        }

        // Update uniform buffer
        vk::DescriptorBufferInfo buffer_info{};
        buffer_info.buffer = buffer_it->second.buffer;
        buffer_info.offset = 0;
        buffer_info.range = VK_WHOLE_SIZE;
        ds.buffer_infos.push_back(buffer_info);

        // Update image sampler
        vk::DescriptorImageInfo image_info{};
        image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        image_info.imageView = texture_it->second.image_view;
        image_info.sampler = samplers_.at(texture_it->second.sampler_handle).sampler;
        ds.image_infos.push_back(image_info);

        // Write descriptor sets
        std::vector<vk::WriteDescriptorSet> descriptor_writes = {
            // Binding 0: Uniform Buffer
            vk::WriteDescriptorSet{ds.descriptor_set, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr,
                                   &ds.buffer_infos.back(), nullptr},
            // Binding 1: Combined Image Sampler
            vk::WriteDescriptorSet{ds.descriptor_set, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler,
                                   &ds.image_infos.back(), nullptr, nullptr}};

        device_.updateDescriptorSets(descriptor_writes, {});
    }

    vk::ShaderModule CreateShaderModule(const std::vector<char>& code) {
        vk::ShaderModuleCreateInfo create_info{};
        create_info.codeSize = code.size();
        create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

        try {
            return device_.createShaderModule(create_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create shader module: ") + e.what());
        }
    }

    void ReleaseResource(const RenderPassHandle& handle) {
        std::lock_guard<std::recursive_mutex> lock_(resource_mutex_);
        auto it = render_passes_.find(handle);
        if (it != render_passes_.end()) {
            if (--it->second.ref_count == 0) {
                device_.destroyRenderPass(it->second.renderpass);
                render_passes_.erase(it);
            }
        }
    }

    vk::Format Convert(Format format) {
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

    // Converts PolygonMode to vk::PolygonMode.
    vk::PolygonMode Convert(PolygonMode mode) {
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
    vk::CullModeFlags Convert(CullMode mode) {
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
    vk::FrontFace Convert(FrontFace face) {
        switch (face) {
            case FrontFace::kCcw:
                return vk::FrontFace::eCounterClockwise;
            case FrontFace::kCw:
                return vk::FrontFace::eClockwise;
            default:
                throw std::runtime_error("Invalid FrontFace.");
        }
    }

    vk::ImageUsageFlags Convert(TextureUsage usage) {
        vk::ImageUsageFlags vk_usage = {};

        if ((usage & TextureUsage::kRenderTarget) == TextureUsage::kRenderTarget) {
            vk_usage |= vk::ImageUsageFlagBits::eColorAttachment;
        }
        if ((usage & TextureUsage::kDepthStencil) == TextureUsage::kDepthStencil) {
            vk_usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
        }
        if ((usage & TextureUsage::kStorage) == TextureUsage::kStorage) {
            vk_usage |= vk::ImageUsageFlagBits::eStorage;
        }
        if ((usage & TextureUsage::kInputAttachment) == TextureUsage::kInputAttachment) {
            vk_usage |= vk::ImageUsageFlagBits::eInputAttachment;
        }

        return vk_usage;
    }

    uint64_t HashDesc(const RenderPassDesc& desc) {
        XXH64_reset(hash_state_, 0);

        for (const auto& format : desc.color_formats) {
            XXH64_update(hash_state_, &format, sizeof(format));
        }

        XXH64_update(hash_state_, &desc.depth_format, sizeof(desc.depth_format));

        for (const auto& clear_color : desc.clear_colors) {
            XXH64_update(hash_state_, clear_color.data(), clear_color.size() * sizeof(float));
        }

        XXH64_update(hash_state_, &desc.clear_depth, sizeof(desc.clear_depth));
        XXH64_update(hash_state_, &desc.clear_depth_value, sizeof(desc.clear_depth_value));
        XXH64_update(hash_state_, &desc.clear_stencil_value, sizeof(desc.clear_stencil_value));

        for (const auto& color_op : desc.color_attachment_options) {
            XXH64_update(hash_state_, &color_op.load_op, sizeof(color_op.load_op));
            XXH64_update(hash_state_, &color_op.store_op, sizeof(color_op.store_op));
        }

        XXH64_update(hash_state_, &desc.depth_attachment_options.load_op,
                     sizeof(desc.depth_attachment_options.load_op));
        XXH64_update(hash_state_, &desc.depth_attachment_options.store_op,
                     sizeof(desc.depth_attachment_options.store_op));

        for (const auto& subpass : desc.subpasses) {
            for (const auto& color_attachment : subpass.color_attachments) {
                XXH64_update(hash_state_, &color_attachment.attachment, sizeof(color_attachment.attachment));
            }
            if (subpass.depth_attachment.attachment != 0) {
                XXH64_update(hash_state_, &subpass.depth_attachment.attachment,
                             sizeof(subpass.depth_attachment.attachment));
            }
        }

        uint64_t hash = XXH64_digest(hash_state_);
        return hash;
    }
};

// Implementation

// Factory method
std::unique_ptr<IRenderer> IRenderer::Create() { return std::make_unique<VulkanRenderer>(); }

}  // namespace lumora
