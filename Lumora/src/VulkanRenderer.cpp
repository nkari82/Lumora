#include <cstring>
#include <fstream>
#include <iostream>
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
#define XXH_NAMESPACE lumora
#include <Lumora/IRenderer.h>
#include <vk_mem_alloc.h>
#include <xxhash.h>

// SPIRV-Cross 헤더 추가
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#define VULKAN_DEBUG_VALIDATION

namespace lumora {

// Constants
const int kMaxFramesInFlight = 2;

// Internal Resource Handle
struct RenderPassHandle : ResourceHandle {};

enum class TextureCreationType { kRegular, kSwapChain };

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

struct VulkanTexture : VulkanRef {
    TextureDesc desc;
    vk::Image image;
    VmaAllocation allocation;
    vk::ImageView image_view;
    SamplerHandle sampler_handle;
    TextureCreationType creation_type = TextureCreationType::kRegular;  // New member
};

struct VulkanSampler : VulkanRef {
    SamplerDesc desc;  // To store sampler configuration
    vk::Sampler sampler;
};

struct VulkanShader : VulkanRef {
    ShaderDesc desc;  // To store shader metadata
    vk::ShaderModule shader_module;

    // Reflection Data
    std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes;
    std::vector<vk::DescriptorSetLayoutBinding> descriptor_set_layout_bindings;
    std::vector<vk::PushConstantRange> push_constant_ranges;
    std::vector<vk::DescriptorSetLayoutBinding> storage_buffer_bindings;
    std::vector<vk::DescriptorSetLayoutBinding> sampler_bindings;
    uint32_t vertex_stride{0};
};

struct VulkanPipeline : VulkanRef {
    PipelineDesc desc;  // Store pipeline configuration
    std::unordered_map<uint64_t, vk::Pipeline> pipelines;
    vk::PipelineLayout layout;
};

// New Structs for Framebuffer and Render Pass
struct VulkanFrameBuffer : VulkanRef {
    uint32_t width;
    uint32_t height;
    std::vector<TextureHandle> color_textures;
    TextureHandle depth_texture;
    std::vector<vk::Framebuffer> framebuffers;  // 스왑체인 이미지별 프레임버퍼
    RenderPassHandle rp_handle;
};

struct VulkanRenderPass : VulkanRef {
    std::vector<vk::ClearValue> clear_values;
    vk::ClearDepthStencilValue clear_depth = {};
    vk::RenderPass renderpass;
    uint64_t desc_hash;
};

struct VulkanSwapChain : VulkanRef {
    SwapChainDesc desc;
    vk::SwapchainKHR swapchain;
    vk::SurfaceKHR surface;
    vk::SurfaceFormatKHR chosen_color_format;
    vk::Format chosen_depth_format = vk::Format::eUndefined;
    vk::PresentModeKHR chosen_present_mode;
    vk::Extent2D chosen_extent;

    // Command Pool
    vk::CommandPool command_pool;
    std::vector<vk::CommandBuffer> command_buffers;

    // Synchronization primitives
    std::vector<vk::Semaphore> image_available_semaphores;
    std::vector<vk::Semaphore> render_finished_semaphores;
    std::vector<vk::Fence> in_flight_fences;
    size_t current_frame;

    FrameBufferHandle fb_handle;
};

struct DescriptorSet {
    vk::DescriptorSet descriptor_set;
    std::vector<vk::DescriptorBufferInfo> buffer_infos;
    std::vector<vk::DescriptorImageInfo> image_infos;
};

class VulkanRenderer : public IRenderer {
   public:
    VulkanRenderer() {}

    ~VulkanRenderer() override {}

    void Open(const char* app_name, const SwapChainDesc& desc) override {
        hash_state_ = XXH64_createState();

        main_window_handle_ = desc.window_handle;

        InitVulkan(app_name, main_window_handle_);

        main_swap_chain_ = CreateSwapChain(desc);

        CreateFrameBuffer(main_swap_chain_);
    }

    void Close() override {
        XXH64_freeState(hash_state_);
        ReleaseResource(main_swap_chain_);
        CleanupVulkan();
    }

    // public
    SwapChainHandle CreateSwapChain(const SwapChainDesc& desc) override {
        VulkanSwapChain swapchain_data;
        swapchain_data.desc = desc;

        swapchain_data.surface = CreateSurface(desc.window_handle);

        auto surface_formats = physical_device_.getSurfaceFormatsKHR(swapchain_data.surface);
        swapchain_data.chosen_color_format = ChooseSurfaceFormat(surface_formats, Convert(desc.color_format));

        auto present_modes = physical_device_.getSurfacePresentModesKHR(swapchain_data.surface);
        swapchain_data.chosen_present_mode = ChoosePresentMode(present_modes);

        auto capabilities = physical_device_.getSurfaceCapabilitiesKHR(swapchain_data.surface);
        swapchain_data.chosen_extent = ChooseExtent(capabilities, desc.width, desc.height);

        if (desc.depth_format != Format::kUndefined)
            swapchain_data.chosen_depth_format = FindDepthFormat(Convert(desc.depth_format));

        uint32_t image_count = desc.buffer_count;
        if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
            image_count = capabilities.maxImageCount;
        }

        vk::SwapchainCreateInfoKHR swapchain_info{};
        swapchain_info.sType = vk::StructureType::eSwapchainCreateInfoKHR;
        swapchain_info.surface = swapchain_data.surface;
        swapchain_info.minImageCount = image_count;
        swapchain_info.imageFormat = swapchain_data.chosen_color_format.format;
        swapchain_info.imageColorSpace = swapchain_data.chosen_color_format.colorSpace;
        swapchain_info.imageExtent = swapchain_data.chosen_extent;
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
        swapchain_info.presentMode = swapchain_data.chosen_present_mode;
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
        buffer_info.usage = Convert(desc.usage);

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
        buffers_.emplace(handle, vbuffer);

        return handle;
    }

    void UpdateBuffer(const BufferHandle& handle, const void* data, size_t size) override {
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
        vtexture.image_view = CreateView(vtexture.image, image_info.format, aspect_mask);

        TextureHandle handle;
        handle.id = GenerateUniqueID();
        textures_.emplace(handle, vtexture);

        return handle;
    }

    void BindTexture(const TextureHandle& handle, uint32_t bind_point) override {
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
        sampler_info.anisotropyEnable = desc.enable_anisotropy ? VK_TRUE : VK_FALSE;
        sampler_info.maxAnisotropy = desc.enable_anisotropy ? desc.max_anisotropy : 1.0f;
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
        samplers_.emplace(handle, vsampler);

        return handle;
    }

    void BindSampler(const SamplerHandle& handle, uint32_t bind_point) override {
        auto sampler_it = samplers_.find(handle);
        if (sampler_it == samplers_.end()) {
            throw std::runtime_error("Invalid SamplerHandle provided to BindSampler.");
        }

        // For simplicity, assume sampler is already bound via descriptor sets when binding textures
        // Additional implementation may be required based on specific use cases
    }

    ShaderHandle CreateShader(const ShaderDesc& desc) override {
        VulkanShader vshader;
        vshader.desc = desc;  // Store shader metadata

        // Load SPIR-V binary from file
        std::ifstream file(desc.file_path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file.");
        }

        size_t file_size = static_cast<size_t>(file.tellg());
        std::vector<uint32_t> spirv_binary(file_size / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(spirv_binary.data()), file_size);
        file.close();

        // Create Vulkan shader module
        vk::ShaderModuleCreateInfo create_info{};
        create_info.codeSize = spirv_binary.size() * sizeof(uint32_t);
        create_info.pCode = spirv_binary.data();

        try {
            vshader.shader_module = device_.createShaderModule(create_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create shader module: ") + e.what());
        }

        // Perform shader reflection using SPIRV-Cross
        try {
            spirv_cross::Compiler compiler(spirv_binary);
            spirv_cross::ShaderResources resources = compiler.get_shader_resources();

            // Extract input attributes (only for vertex shaders)
            if (desc.stage == ShaderStage::kVertex) {
                for (const auto& input : resources.stage_inputs) {
                    spirv_cross::SPIRType type = compiler.get_type(input.type_id);
                    uint32_t location = compiler.get_decoration(input.id, spv::DecorationLocation);
                    uint32_t binding = compiler.get_decoration(input.id, spv::DecorationBinding);
                    uint32_t offset = compiler.get_decoration(input.id, spv::DecorationOffset);

                    // Determine Vulkan format from SPIRType
                    vk::Format format = Convert(type);

                    // Populate vk::VertexInputAttributeDescription
                    vk::VertexInputAttributeDescription attr_desc{};
                    attr_desc.location = location;
                    attr_desc.binding = binding;  // Typically 0 for single binding
                    attr_desc.format = format;
                    attr_desc.offset = offset;
                    vshader.vertex_input_attributes.push_back(attr_desc);
                    vshader.vertex_stride += GetFormatSize(format);
                }
            }

            // Extract descriptor bindings (Uniform Buffers and Sampled Images)
            for (const auto& resource : resources.uniform_buffers) {
                uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                vk::DescriptorType type = vk::DescriptorType::eUniformBuffer;
                uint32_t count = 1;  // Adjust if using arrays
                vk::ShaderStageFlags stage_flags = Convert(desc.stage);

                // Populate vk::DescriptorSetLayoutBinding
                vk::DescriptorSetLayoutBinding layout_binding{};
                layout_binding.binding = binding;
                layout_binding.descriptorType = type;
                layout_binding.descriptorCount = count;
                layout_binding.stageFlags = stage_flags;
                layout_binding.pImmutableSamplers = nullptr;  // Optional

                vshader.descriptor_set_layout_bindings.push_back(layout_binding);
            }

            for (const auto& resource : resources.sampled_images) {
                uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                vk::DescriptorType type = vk::DescriptorType::eCombinedImageSampler;
                uint32_t count = 1;  // Adjust if using arrays
                vk::ShaderStageFlags stage_flags = Convert(desc.stage);

                // Populate vk::DescriptorSetLayoutBinding
                vk::DescriptorSetLayoutBinding layout_binding{};
                layout_binding.binding = binding;
                layout_binding.descriptorType = type;
                layout_binding.descriptorCount = count;
                layout_binding.stageFlags = stage_flags;
                layout_binding.pImmutableSamplers = nullptr;  // Optional

                vshader.descriptor_set_layout_bindings.push_back(layout_binding);
            }

            // Extract storage buffers
            for (const auto& resource : resources.storage_buffers) {
                uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                vk::DescriptorType type = vk::DescriptorType::eStorageBuffer;
                uint32_t count = 1;  // Adjust if using arrays
                vk::ShaderStageFlags stage_flags = Convert(desc.stage);

                // Populate vk::DescriptorSetLayoutBinding
                vk::DescriptorSetLayoutBinding layout_binding{};
                layout_binding.binding = binding;
                layout_binding.descriptorType = type;
                layout_binding.descriptorCount = count;
                layout_binding.stageFlags = stage_flags;
                layout_binding.pImmutableSamplers = nullptr;  // Optional

                vshader.storage_buffer_bindings.push_back(layout_binding);
            }

            // Extract samplers (if separate from sampled images)
            for (const auto& resource : resources.separate_samplers) {
                uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                vk::DescriptorType type = vk::DescriptorType::eSampler;
                uint32_t count = 1;  // Adjust if using arrays
                vk::ShaderStageFlags stage_flags = Convert(desc.stage);

                // Populate vk::DescriptorSetLayoutBinding
                vk::DescriptorSetLayoutBinding layout_binding{};
                layout_binding.binding = binding;
                layout_binding.descriptorType = type;
                layout_binding.descriptorCount = count;
                layout_binding.stageFlags = stage_flags;
                layout_binding.pImmutableSamplers = nullptr;  // Optional

                vshader.sampler_bindings.push_back(layout_binding);
            }

            // Extract push constants
            auto push_constants = compiler.get_shader_resources().push_constant_buffers;
            for (const auto& push_constant : push_constants) {
                spirv_cross::SPIRType type = compiler.get_type(push_constant.type_id);
                uint32_t offset = compiler.get_decoration(push_constant.id, spv::DecorationOffset);
                uint32_t size = compiler.get_declared_struct_size(type);

                vk::ShaderStageFlags stage_flags = Convert(desc.stage);

                // Populate vk::PushConstantRange
                vk::PushConstantRange push_constant_range{};
                push_constant_range.stageFlags = stage_flags;
                push_constant_range.offset = offset;
                push_constant_range.size = size;

                vshader.push_constant_ranges.push_back(push_constant_range);
            }

            // Additional resource types (e.g., storage buffers, separate samplers) are handled similarly
        } catch (const spirv_cross::CompilerError& e) {
            throw std::runtime_error(std::string("SPIRV-Cross reflection error: ") + e.what());
        }

        // Create unique ShaderHandle and store the shader
        ShaderHandle handle;
        handle.id = GenerateUniqueID();
        shaders_.emplace(handle, vshader);

        return handle;
    }

    PipelineHandle CreatePipeline(const PipelineDesc& desc) override {
        VulkanPipeline vpipeline;
        vpipeline.desc = desc;  // Store pipeline description

        // Setup shader stages
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

        // Select a shader to base the pipeline layout on (e.g., vertex shader)
        VulkanShader* base_shader = nullptr;
        if (desc.vertex_shader.id != 0) {
            base_shader = &shaders_.at(desc.vertex_shader);
        } else if (desc.fragment_shader.id != 0) {
            base_shader = &shaders_.at(desc.fragment_shader);
        }

        if (!base_shader) {
            throw std::runtime_error("No shader available to create pipeline layout.");
        }

        // Create Pipeline Layout based on shader reflection data
        vpipeline.layout = CreatePipelineLayout(*base_shader);

        // Vertex Input Binding Descriptions
        std::vector<vk::VertexInputBindingDescription> binding_descriptions = {
            vk::VertexInputBindingDescription{0, base_shader->vertex_stride, vk::VertexInputRate::eVertex}};

        // Vertex Input Attribute Descriptions
        std::vector<vk::VertexInputAttributeDescription> attribute_descriptions = base_shader->vertex_input_attributes;

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
        scissor.offset =
            vk::Offset2D{static_cast<int32_t>(desc.scissor.offset_x), static_cast<int32_t>(desc.scissor.offset_y)};
        scissor.extent = vk::Extent2D{desc.scissor.width, desc.scissor.height};

        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;

        // Rasterizer Configuration
        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = desc.rasterization.depth_clamp_enable;
        rasterizer.rasterizerDiscardEnable = desc.rasterization.rasterizer_discard_enable;
        rasterizer.polygonMode = Convert(desc.rasterization.polygon_mode);
        rasterizer.lineWidth = 1.0f;  // Can be adjusted
        rasterizer.cullMode = Convert(desc.rasterization.cull_mode);
        rasterizer.frontFace = Convert(desc.rasterization.front_face);
        rasterizer.depthBiasEnable = VK_FALSE;

        // Multisampling Configuration
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = Convert(desc.sample_count);

        // Depth Stencil Configuration
        vk::PipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.depthTestEnable = desc.depth_stencil.depth_test_enable;
        depth_stencil.depthWriteEnable = desc.depth_stencil.depth_write_enable;
        depth_stencil.depthCompareOp = Convert(desc.depth_stencil.depth_compare_op);
        depth_stencil.depthBoundsTestEnable = VK_FALSE;
        depth_stencil.stencilTestEnable = desc.depth_stencil.stencil_test_enable;
        // Additional stencil settings can be configured here

        // Color Blending Configuration
        std::vector<vk::PipelineColorBlendAttachmentState> color_blend_attachments;
        for (const auto& blend_state : desc.color_blends) {
            vk::PipelineColorBlendAttachmentState color_blend{};
            color_blend.blendEnable = blend_state.blend_enable;
            color_blend.srcColorBlendFactor = Convert(blend_state.src_color_blend_factor);
            color_blend.dstColorBlendFactor = Convert(blend_state.dst_color_blend_factor);
            color_blend.colorBlendOp = Convert(blend_state.color_blend_op);
            color_blend.srcAlphaBlendFactor = Convert(blend_state.src_alpha_blend_factor);
            color_blend.dstAlphaBlendFactor = Convert(blend_state.dst_alpha_blend_factor);
            color_blend.alphaBlendOp = Convert(blend_state.alpha_blend_op);
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

        // Pipeline Layout is already created based on shader reflection data
        // Use the created pipeline_layout

        // Graphics Pipeline Creation
        vk::GraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
        pipeline_info.pStages = shader_stages.data();
        pipeline_info.pVertexInputState = &vertex_input_info;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.layout = vpipeline.layout;
        // pipeline_info.renderPass = render_pass_;  // Use the appropriate render pass
        pipeline_info.subpass = 0;
        pipeline_info.basePipelineHandle = nullptr;

        try {
            vpipeline.pipelines[0] = device_.createGraphicsPipeline(nullptr, pipeline_info).value;
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create graphics pipeline: ") + e.what());
        }

        // Store the pipeline with a unique handle
        PipelineHandle handle;
        handle.id = GenerateUniqueID();
        pipelines_.emplace(handle, vpipeline);

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

        try {
            vpipeline.pipelines[0] = device_.createComputePipeline(nullptr, pipeline_info).value;
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create compute pipeline: ") + e.what());
        }

        PipelineHandle handle;
        handle.id = GenerateUniqueID();
        pipelines_.emplace(handle, vpipeline);

        return handle;
    }

    // BeginPass시 RenderDesc로 CreateFrameBuffer를 생성하고 CreateRenderPass를 생성한다.
    FrameBufferHandle CreateFrameBuffer(const FrameBufferDesc& desc) {
        std::vector<Format> color_formats;
        Format depth_format;

        for (const auto& texture_handle : desc.color_targets) {
            auto texture_it = textures_.find(texture_handle);
            if (texture_it == textures_.end()) {
                throw std::runtime_error("Invalid TextureHandle in FrameBufferDesc.color_targets.");
            }
            color_formats.push_back(texture_it->second.desc.format);
        }

        if (desc.depth_target.id != 0) {  // Assuming TextureHandle{0} is invalid
            auto depth_it = textures_.find(desc.depth_target);
            if (depth_it == textures_.end()) {
                throw std::runtime_error("Invalid TextureHandle in FrameBufferDesc.depth_target.");
            }
            depth_format = depth_it->second.desc.format;
        } else {
            depth_format = Format::kUndefined;
        }

        // Create or retrieve RenderPass
        RenderPassHandle rp_handle = CreateRenderPassInternal(color_formats, depth_format, desc.config);

        // Create Framebuffer
        VulkanFrameBuffer vframebuffer;

        // Gather image views for attachments
        std::vector<vk::ImageView> attachments;
        for (const auto& color_handle : desc.color_targets) {
            auto it = textures_.find(color_handle);
            if (it == textures_.end()) {
                throw std::runtime_error("Invalid TextureHandle in FrameBufferDesc.color_targets.");
            }
            attachments.push_back(it->second.image_view);
            it->second.ref_count++;
            vframebuffer.color_textures.emplace_back(color_handle);
        }

        if (desc.depth_target.id != 0) {
            auto it = textures_.find(desc.depth_target);
            if (it == textures_.end()) {
                throw std::runtime_error("Invalid TextureHandle in FrameBufferDesc.depth_target.");
            }

            attachments.push_back(it->second.image_view);
            it->second.ref_count++;
            vframebuffer.depth_texture = desc.depth_target;
        }

        // Retrieve the RenderPass
        auto render_pass_it = renderpasses_.find(rp_handle);
        if (render_pass_it == renderpasses_.end()) {
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

        vframebuffer.rp_handle = rp_handle;
        vframebuffer.width = width;
        vframebuffer.height = height;

        vframebuffer.ref_count = 1;

        FrameBufferHandle handle;
        handle.id = GenerateUniqueID();
        framebuffers_.emplace(handle, vframebuffer);

        return handle;
    }

    FrameBufferHandle CreateFrameBuffer(const SwapChainHandle& handle) override {
        // SwapChain 조회
        auto swapchain_it = swapchains_.find(handle);
        if (swapchain_it == swapchains_.end()) {
            throw std::runtime_error("Invalid SwapChainHandle provided to CreateFrameBuffer.");
        }

        VulkanSwapChain& sc_data = swapchain_it->second;
        bool has_depth = (sc_data.chosen_depth_format != vk::Format::eUndefined);

        // 스왑체인 이미지 가져오기
        std::vector<vk::Image> swapchain_images = device_.getSwapchainImagesKHR(sc_data.swapchain);

        // RenderPass 생성 (SwapChainDesc를 기반으로)
        std::vector<Format> color_formats;
        Format depth_format;
        color_formats.emplace_back(Convert(sc_data.chosen_color_format.format));
        depth_format = Convert(sc_data.chosen_depth_format);
        RenderPassConfig config{};
        config.clear_colors = {{0.0f, 0.0f, 0.0f, 1.0f}};
        config.clear_depth = true;
        config.clear_depth_value = 1.0f;
        config.clear_stencil_value = 0;
        config.color_attachment_options = {
            AttachmentOptions{.load_op = AttachmentLoadOp::kClear, .store_op = AttachmentStoreOp::kStore}};
        if (has_depth) {
            config.depth_attachment_options =
                AttachmentOptions{.load_op = AttachmentLoadOp::kClear, .store_op = AttachmentStoreOp::kStore};
        }

        // 서브패스 설정
        SubpassDesc subpass;

        // 컬러 어태치먼트 참조
        uint32_t colorAttachmentRef = 0;  // 첫 번째 컬러 어태치먼트 인덱스

        subpass.color_attachments.push_back(colorAttachmentRef);

        // 깊이 어태치먼트 참조
        if (has_depth) {
            uint32_t depthAttachmentRef = 0;  // 깊이 어태치먼트는 인덱스 0으로 가정
            subpass.depth_attachment = depthAttachmentRef;
        }

        config.subpasses.push_back(subpass);

        RenderPassHandle rp_handle = CreateRenderPassInternal(color_formats, depth_format, config);

        // VulkanFrameBuffer 생성
        VulkanFrameBuffer vframebuffer;
        vframebuffer.rp_handle = rp_handle;

        // 깊이 텍스처가 필요한 경우
        TextureHandle depth_handle = TextureHandle{0};
        if (has_depth) {
            // 깊이 텍스처 생성
            depth_handle = CreateTexture({
                .type = TextureType::k2D,
                .format = Convert(sc_data.chosen_depth_format),
                .usage = TextureUsage::kDepthStencil,
                .width = sc_data.chosen_extent.width,
                .height = sc_data.chosen_extent.height,
                .depth = 1,
                .mip_levels = 1,
                .array_layers = 1,
                .memory_usage = MemoryUsage::kGpuOnly,  // 필요에 따라 조정
            });
            vframebuffer.depth_texture = depth_handle;
        }

        for (const auto& image : swapchain_images) {
            // CreateView 메소드를 사용하여 이미지 뷰 생성
            vk::ImageView image_view =
                CreateView(image, sc_data.chosen_color_format.format, vk::ImageAspectFlagBits::eColor);

            // TextureHandle 생성 (kSwapChain 타입)
            TextureHandle texture_handle;
            texture_handle.id = GenerateUniqueID();

            // VulkanTexture 구조체 채우기
            VulkanTexture vtexture;
            vtexture.desc = {
                .type = TextureType::k2D,
                .format = Convert(sc_data.chosen_color_format.format),
                .usage = TextureUsage::kRenderTarget,
                .width = sc_data.chosen_extent.width,
                .height = sc_data.chosen_extent.height,
                .depth = 1,
                .mip_levels = 1,
                .array_layers = 1,
            };

            vtexture.image = image;
            vtexture.image_view = image_view;
            vtexture.creation_type = TextureCreationType::kSwapChain;

            textures_.emplace(texture_handle, vtexture);
            vframebuffer.color_textures.emplace_back(texture_handle);

            // Framebuffer 생성 정보 설정
            std::vector<vk::ImageView> attachments = {image_view};

            if (has_depth) {
                auto depth_it = textures_.find(depth_handle);
                if (depth_it != textures_.end())
                    attachments.emplace_back(depth_it->second.image_view);
            }

            // FramebufferCreateInfo 설정
            vk::FramebufferCreateInfo framebuffer_info{};
            framebuffer_info.renderPass = renderpasses_.at(rp_handle).renderpass;
            framebuffer_info.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebuffer_info.pAttachments = attachments.data();
            framebuffer_info.width = sc_data.chosen_extent.width;
            framebuffer_info.height = sc_data.chosen_extent.height;
            framebuffer_info.layers = 1;

            // Framebuffer 생성
            vk::Framebuffer framebuffer;
            try {
                framebuffer = device_.createFramebuffer(framebuffer_info);
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Failed to create framebuffer: ") + e.what());
            }

            vframebuffer.width = sc_data.chosen_extent.width;
            vframebuffer.height = sc_data.chosen_extent.height;
            vframebuffer.framebuffers.emplace_back(framebuffer);
        }

        FrameBufferHandle fb_handle;
        fb_handle.id = GenerateUniqueID();
        framebuffers_.emplace(fb_handle, vframebuffer);
        sc_data.fb_handle = fb_handle;
        return fb_handle;
    }

    void BindPipeline(const PipelineHandle& handle, const uint8_t* constants, size_t size,
                      uint32_t sub_index = 0) override {
        // Retrieve VulkanPipeline
        auto pipeline_it = pipelines_.find(handle);
        if (pipeline_it == pipelines_.end()) {
            throw std::runtime_error("Invalid PipelineHandle provided to BindPipeline.");
        }

        VulkanPipeline& vpipeline = pipeline_it->second;

        // 파이프라인 해시 키를 사용하여 특정 서브패스에 대한 파이프라인을 가져옴
        uint64_t pipeline_key = sub_index;  // 서브패스 인덱스를 키로 사용 (더 복잡한 경우 해시 사용 가능)

        VulkanRenderPass current_render_pass;
        uint64_t render_pass_hash = current_render_pass.desc_hash;
        uint32_t current_pass = current_pass_;  // Current subpass index

        // Combine render pass hash and subpass index to create a unique key
        uint64_t combined_hash = render_pass_hash ^ (static_cast<uint64_t>(current_pass) << 32);

        // Check if pipeline with combined_hash exists
        auto existing_pipeline_it = vpipeline.pipelines.find(combined_hash);
        if (existing_pipeline_it != vpipeline.pipelines.end()) {
            // Pipeline already exists, bind it
            command_buffer_.bindPipeline(vk::PipelineBindPoint::eGraphics, existing_pipeline_it->second);
        } else {
            // Create a new pipeline based on the stored desc
            // 여기서는 기존 파이프라인 정보를 재사용하여 새로운 파이프라인을 생성
            vk::GraphicsPipelineCreateInfo pipeline_info = {};

            // 셰이더 스테이지 설정
            pipeline_info.stageCount =
                static_cast<uint32_t>(vpipeline.desc.vertex_shader.id != 0 ? 2 : 1);  // 간단히 설정
            pipeline_info.pStages = nullptr;                                          // 이미 CreatePipeline에서 생성됨

            // Vertex Input State
            // 이미 CreatePipeline에서 설정됨

            // Input Assembly, Viewport, Rasterizer, Multisampling, Depth Stencil, Color Blending 등
            // 이미 CreatePipeline에서 설정됨

            // Pipeline Layout 및 Render Pass 설정
            pipeline_info.layout = vpipeline.layout;
            pipeline_info.renderPass = render_pass_;

            // 새로운 파이프라인 생성
            try {
                vk::Pipeline new_pipeline = device_.createGraphicsPipeline(nullptr, pipeline_info).value;
                vpipeline.pipelines.emplace(pipeline_key, new_pipeline);
                // Bind the new pipeline
                command_buffer_.bindPipeline(vk::PipelineBindPoint::eGraphics, new_pipeline);
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Failed to create graphics pipeline: ") + e.what());
            }
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

    void BeginPass(const FrameBufferHandle& handle) override {
        uint32_t image_index{0};
        // 실제 FB 결정
        FrameBufferHandle actual_fb = handle;
        if (actual_fb.id == 0) {
            actual_fb = current_fb_handle_;
            image_index = current_image_index_;
        }

        auto fb_it = framebuffers_.find(actual_fb);
        if (fb_it == framebuffers_.end()) {
            throw std::runtime_error("Invalid FrameBufferHandle in BeginPass.");
        }

        VulkanFrameBuffer& vframebuffer = fb_it->second;
        VulkanRenderPass& vrenderpass = renderpasses_.at(vframebuffer.rp_handle);
        current_render_pass_handle_ = vframebuffer.rp_handle;

        // RenderPass 시작
        vk::RenderPassBeginInfo render_pass_info{};
        render_pass_info.renderPass = vrenderpass.renderpass;
        render_pass_info.framebuffer = vframebuffer.framebuffers[image_index];
        render_pass_info.renderArea.offset = vk::Offset2D{0, 0};
        render_pass_info.renderArea.extent = vk::Extent2D{vframebuffer.width, vframebuffer.height};
        render_pass_info.clearValueCount = static_cast<uint32_t>(vrenderpass.clear_values.size());
        render_pass_info.pClearValues = vrenderpass.clear_values.data();

        try {
            command_buffer_.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to begin render pass: ") + e.what());
        }
    }

    void EndPass() override { command_buffer_.endRenderPass(); }

    void NextPass() override {
        command_buffer_.nextSubpass(vk::SubpassContents::eInline);
        current_pass_++;
    }

    void Resize(uint32_t new_width, uint32_t new_height) override { Resize(main_swap_chain_, new_width, new_height); }

    void Resize(const SwapChainHandle& handle, uint32_t new_width, uint32_t new_height) override {
        auto it = swapchains_.find(handle);
        if (it == swapchains_.end()) {
            return;  // 잘못된 핸들이면 무시
        }
        VulkanSwapChain& sc_data = it->second;

        // 1) GPU 대기
        device_.waitIdle();

        // 2) 백업: 기존 스왑체인 handle
        vk::SwapchainKHR old_swapchain = sc_data.swapchain;

        // 3) 백업: 기존 프레임버퍼
        auto old_fb_handle = sc_data.fb_handle;

        // 4) 새 스왑체인 정보
        auto capabilities = physical_device_.getSurfaceCapabilitiesKHR(sc_data.surface);
        sc_data.chosen_extent = ChooseExtent(capabilities, new_width, new_height);

        // 5) createInfo에 oldSwapchain 설정
        vk::SwapchainCreateInfoKHR sci{};
        sci.surface = sc_data.surface;
        sci.minImageCount = std::max<uint32_t>(2u, static_cast<uint32_t>(sc_data.desc.buffer_count));
        sci.imageFormat = sc_data.chosen_color_format.format;
        sci.imageColorSpace = sc_data.chosen_color_format.colorSpace;
        sci.imageExtent = sc_data.chosen_extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        sci.presentMode = sc_data.chosen_present_mode;
        sci.clipped = VK_TRUE;
        sci.oldSwapchain = old_swapchain;  // 구 스왑체인 지정!

        // 6) 새 스왑체인 생성
        sc_data.swapchain = device_.createSwapchainKHR(sci);

        // 7) 구 스왑체인은 여기서 destroy
        //    새 스왑체인 생성 후 oldSwapchain을 안전하게 파괴할 수 있음
        if (old_swapchain) {
            device_.destroySwapchainKHR(old_swapchain);
        }

        // 업데이트 정보
        sc_data.desc.width = new_width;
        sc_data.desc.height = new_height;
        sc_data.current_frame = 0;

        // 8) 새 스왑체인 이미지 기반 프레임버퍼 생성
        sc_data.fb_handle = CreateFrameBuffer(handle);

        // 9) 구 프레임버퍼 제거.
        ReleaseResource(old_fb_handle);
    }

    void Render(std::function<void()> callback) override { Render(main_swap_chain_, callback); }

    void Render(const SwapChainHandle& handle, std::function<void()> callback) override {
        auto it = swapchains_.find(handle);
        if (it == swapchains_.end()) {
            throw std::runtime_error("Invalid SwapChainHandle provided to Render.");
        }

        VulkanSwapChain& sc_data = it->second;
        VulkanFrameBuffer& vframebuffer = framebuffers_.at(sc_data.fb_handle);
        current_fb_handle_ = sc_data.fb_handle;

        // Synchronization primitives
        size_t frame = sc_data.current_frame;
        vk::Semaphore image_available_semaphore = sc_data.image_available_semaphores[frame];
        vk::Semaphore render_finished_semaphore = sc_data.render_finished_semaphores[frame];
        vk::Fence in_flight_fence = sc_data.in_flight_fences[frame];

        // Wait for the previous frame to finish
        std::ignore = device_.waitForFences(in_flight_fence, VK_TRUE, UINT64_MAX);

        // Reset the fence for the current frame
        device_.resetFences(in_flight_fence);

        // Acquire the next image from the swapchain
        vk::Result result = device_.acquireNextImageKHR(sc_data.swapchain, UINT64_MAX, image_available_semaphore,
                                                        nullptr, &current_image_index_);
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
        callback();

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
        present_info.pImageIndices = &current_image_index_;

        try {
            vk::Result present_result = graphics_queue_.presentKHR(present_info);
            if (present_result == vk::Result::eErrorOutOfDateKHR || present_result == vk::Result::eSuboptimalKHR) {
                throw std::runtime_error("Swapchain is out of date or suboptimal.");
            } else if (present_result != vk::Result::eSuccess) {
                throw std::runtime_error("Failed to present swapchain image.");
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to present swapchain image: ") + e.what());
            return;
        }

        // 다음 프레임으로 이동
        sc_data.current_frame = (sc_data.current_frame + 1) % kMaxFramesInFlight;
    }

    void DrawIndexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0,
                     int32_t vertex_offset = 0, uint32_t first_instance = 0) override {
        command_buffer_.drawIndexed(index_count, instance_count, first_index, vertex_offset, first_instance);
    }

    bool ReloadShader(const ShaderHandle& handle, const ShaderDesc& new_desc) override {
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
        if (it == swapchains_.end())
            return;

        device_.waitIdle();

        VulkanSwapChain& sc_data = it->second;

        for (auto& sem : sc_data.image_available_semaphores) {
            device_.destroySemaphore(sem);
        }
        for (auto& sem : sc_data.render_finished_semaphores) {
            device_.destroySemaphore(sem);
        }
        for (auto& f : sc_data.in_flight_fences) {
            device_.destroyFence(f);
        }
        if (sc_data.command_pool) {
            device_.destroyCommandPool(sc_data.command_pool);
        }
        if (sc_data.swapchain) {
            device_.destroySwapchainKHR(sc_data.swapchain);
        }

        // frame_buffer 해제
        if (sc_data.fb_handle.id != 0) {
            ReleaseResource(sc_data.fb_handle);
        }

        // surface 해제 여부
        if (handle.id != main_swap_chain_.id && sc_data.surface) {
            instance_.destroySurfaceKHR(sc_data.surface);
        }

        swapchains_.erase(it);
    }

    void ReleaseResource(const TextureHandle& handle) override {
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
        auto it = pipelines_.find(handle);
        if (it != pipelines_.end()) {
            // Decrement ref count
            if (--it->second.ref_count == 0) {
                // Destroy all pipelines in the map
                for (auto& [key, pipeline] : it->second.pipelines) {
                    device_.destroyPipeline(pipeline);
                }
                device_.destroyPipelineLayout(it->second.layout);
                pipelines_.erase(it);
            }
        }
    }

    void ReleaseResource(const ShaderHandle& handle) override {
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
        auto it = framebuffers_.find(handle);
        if (it != framebuffers_.end()) {
            if (--it->second.ref_count == 0) {
                for (auto& framebuffer : it->second.framebuffers) device_.destroyFramebuffer(framebuffer);
                ReleaseResource(it->second.rp_handle);
                for (auto& h : it->second.color_textures) ReleaseResource(h);
                ReleaseResource(it->second.depth_texture);
                framebuffers_.erase(it);
            }
        }
    }

   private:
    // Vulkan core components
    vk::Instance instance_;
    vk::SurfaceKHR main_surface_{nullptr};
    vk::PhysicalDevice physical_device_;
    vk::Device device_;
    vk::RenderPass render_pass_;
    vk::CommandBuffer command_buffer_;  // current command buffer

    WindowHandle main_window_handle_;
    SwapChainHandle main_swap_chain_;
    XXH64_state_t* hash_state_{nullptr};
    FrameBufferHandle current_fb_handle_;
    uint32_t current_image_index_{0};

    uint32_t graphics_queue_family_;  // Graphics Queue Family Index
    uint32_t present_queue_family_;   // Present Queue Family Index
    vk::Queue graphics_queue_;        // Graphics Queue

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
    std::unordered_map<RenderPassHandle, VulkanRenderPass, HandleHash> renderpasses_;

    // Descriptor Set Management
    vk::DescriptorPool descriptor_pool_;
    vk::DescriptorSetLayout descriptor_set_layout_;  // #TODO 내부적으로 자동 관리

    // Current pipeline handle
    vk::Pipeline current_pipeline_;
    vk::PipelineLayout pipeline_layout_;  // #TODO 내부적으로 자동 관리
    uint32_t current_pass_ = 0;
    RenderPassHandle current_render_pass_handle_;

    // Internal methods
    void InitVulkan(const char* app_name, const WindowHandle& wh) {
        // Create Vulkan Instance
        CreateInstance(app_name);

        main_surface_ = CreateSurface(wh);

        PickPhysicalDevice(main_surface_);

        CreateLogicalDevice(main_surface_);

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

        bool memory_leak = false;

        // Destroy all pipelines
        for (auto& [handle, pipeline] : pipelines_) {
            for (auto& [key, vk_pipeline] : pipeline.pipelines) {
                device_.destroyPipeline(vk_pipeline);
            }
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
            for (auto& fb : framebuffer.framebuffers) {
                device_.destroyFramebuffer(fb);
            }
            ReleaseResource(framebuffer.rp_handle);
            if (framebuffer.ref_count != 0) {
                memory_leak = true;
                std::cerr << "Memory Leak: FrameBuffer ID " << handle.id << " has refCount " << framebuffer.ref_count
                          << std::endl;
            }
        }
        framebuffers_.clear();

        // Destroy all render passes
        for (auto& [handle, render_pass_struct] : renderpasses_) {
            device_.destroyRenderPass(render_pass_struct.renderpass);
            if (render_pass_struct.ref_count != 0) {
                memory_leak = true;
                std::cerr << "Memory Leak: RenderPass ID " << handle.id << " has refCount "
                          << render_pass_struct.ref_count << std::endl;
            }
        }
        renderpasses_.clear();

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

        if (descriptor_set_layout_) {
            device_.destroyDescriptorSetLayout(descriptor_set_layout_);
        }

        if (pipeline_layout_) {
            device_.destroyPipelineLayout(pipeline_layout_);
        }

        instance_.destroySurfaceKHR(main_surface_);

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

        if (CheckValidationLayerSupport()) {
            // Enable validation layers
            const std::vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"};

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
            debug_create_info.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
                                                   void* p_user_data) -> VkBool32 {
                std::cerr << "Validation Layer: " << p_callback_data->pMessage << std::endl;
                return VK_FALSE;
            };

            create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debug_create_info;
        }

        // Create instance
        try {
            instance_ = vk::createInstance(create_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create Vulkan instance: ") + e.what());
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
        if (present_queue_family_ != graphics_queue_family_) {
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

    vk::SurfaceFormatKHR ChooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& available_formats,
                                             vk::Format request_format) {
        request_format = (request_format == vk::Format::eUndefined) ? vk::Format::eB8G8R8A8Unorm : request_format;
        for (const auto& available_format : available_formats) {
            if (available_format.format == request_format &&
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
        bool is_main_window = (window_handle.display == main_window_handle_.display) &&
                              (window_handle.platform == main_window_handle_.platform);

        if (is_main_window && main_surface_) {
            return main_surface_;
        }

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

    RenderPassHandle CreateRenderPassInternal(const std::vector<Format>& color_formats, const Format& depth_format,
                                              const RenderPassConfig& config) {
        // Hash the RenderPassDesc to use as a key
        uint64_t hash_key = HashDesc(color_formats, depth_format, config);

        // Create a unique handle
        RenderPassHandle handle{hash_key};

        // Check if render pass already exists
        auto it = renderpasses_.find(handle);
        if (it != renderpasses_.end()) {
            it->second.ref_count++;
            return it->first;
        }

        // 1. 첨부 지점 변환
        std::vector<vk::AttachmentDescription> attachments;
        attachments.reserve(color_formats.size() + (depth_format != Format::kUndefined ? 1 : 0));

        // 컬러 첨부 지점
        for (size_t i = 0; i < color_formats.size(); ++i) {
            vk::AttachmentDescription attachment = {};
            attachment.format = Convert(color_formats[i]);
            attachment.samples = vk::SampleCountFlagBits::e1;  // 예시, 실제 샘플링은 사용자 입력에 따라 다름
            attachment.loadOp = Convert(config.color_attachment_options[i].load_op);
            attachment.storeOp = Convert(config.color_attachment_options[i].store_op);
            attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            attachment.initialLayout = vk::ImageLayout::eUndefined;
            // 컬러 첨부의 최종 레이아웃을 ePresentSrcKHR로 설정 (예시)
            attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
            attachments.push_back(attachment);
        }

        // 깊이 첨부 지점 (옵션)
        bool has_depth = depth_format != Format::kUndefined;
        size_t depthAttachmentIndex = attachments.size();  // 인덱스
        if (has_depth) {
            vk::AttachmentDescription depth_attachment = {};
            depth_attachment.format = Convert(depth_format);
            depth_attachment.samples = vk::SampleCountFlagBits::e1;  // 예시
            depth_attachment.loadOp = Convert(config.depth_attachment_options.load_op);
            depth_attachment.storeOp = Convert(config.depth_attachment_options.store_op);
            depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
            depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            attachments.push_back(depth_attachment);
        }

        // 2. 서브패스 변환
        std::vector<vk::SubpassDescription> subpasses;
        subpasses.reserve(config.subpasses.size());

        // 서브패스의 컬러, 입력, 깊이 첨부 참조를 저장할 임시 벡터
        std::vector<std::vector<vk::AttachmentReference>> color_attachment_refs;
        std::vector<std::vector<vk::AttachmentReference>> input_attachment_refs;
        std::vector<vk::AttachmentReference> depth_attachment_refs;  // 깊이 첨부 참조 저장
        for (const auto& s : config.subpasses) {
            vk::SubpassDescription subpass = {};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;

            // 컬러 첨부 참조
            std::vector<vk::AttachmentReference> color_refs;
            color_refs.reserve(s.color_attachments.size());
            for (const auto& attachment : s.color_attachments) {
                vk::AttachmentReference ref = {};
                ref.attachment = attachment;
                ref.layout = vk::ImageLayout::eColorAttachmentOptimal;
                color_refs.push_back(ref);
            }
            color_attachment_refs.emplace_back(std::move(color_refs));
            subpass.colorAttachmentCount = static_cast<uint32_t>(color_attachment_refs.back().size());
            subpass.pColorAttachments = color_attachment_refs.back().data();

            // 입력 첨부 참조
            std::vector<vk::AttachmentReference> input_refs;
            input_refs.reserve(s.input_attachments.size());
            for (const auto& attachment : s.input_attachments) {
                vk::AttachmentReference ref = {};
                ref.attachment = attachment;
                ref.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
                input_refs.push_back(ref);
            }
            input_attachment_refs.emplace_back(std::move(input_refs));
            subpass.inputAttachmentCount = static_cast<uint32_t>(input_attachment_refs.back().size());
            subpass.pInputAttachments = input_attachment_refs.back().data();

            // 깊이 첨부 참조
            if (s.depth_attachment.has_value()) {
                vk::AttachmentReference ref = {};
                ref.attachment = depthAttachmentIndex;
                ref.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                depth_attachment_refs.push_back(ref);
                subpass.pDepthStencilAttachment = &depth_attachment_refs.back();
            }

            subpasses.push_back(subpass);
        }

        // 3. 서브패스 의존성 자동 설정
        std::vector<vk::SubpassDependency> dependencies;

        if (!config.subpasses.empty()) {
            // 첫 번째 서브패스에 대한 외부 의존성
            vk::SubpassDependency externalToFirst = {};
            externalToFirst.srcSubpass = VK_SUBPASS_EXTERNAL;
            externalToFirst.dstSubpass = 0;
            externalToFirst.srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
            externalToFirst.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            externalToFirst.srcAccessMask = vk::AccessFlags();  // NONE
            externalToFirst.dstAccessMask =
                vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
            externalToFirst.dependencyFlags = vk::DependencyFlags();
            dependencies.push_back(externalToFirst);

            // 서브패스 간의 의존성 설정
            for (size_t i = 1; i < config.subpasses.size(); ++i) {
                vk::SubpassDependency dep = {};
                dep.srcSubpass = static_cast<uint32_t>(i - 1);
                dep.dstSubpass = static_cast<uint32_t>(i);
                dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
                dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
                dep.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
                dep.dstAccessMask =
                    vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
                dep.dependencyFlags = vk::DependencyFlags();
                dependencies.push_back(dep);
            }

            // 마지막 서브패스에 대한 외부 의존성
            vk::SubpassDependency lastToExternal = {};
            lastToExternal.srcSubpass = static_cast<uint32_t>(config.subpasses.size() - 1);
            lastToExternal.dstSubpass = VK_SUBPASS_EXTERNAL;
            lastToExternal.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            lastToExternal.dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
            lastToExternal.srcAccessMask =
                vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
            lastToExternal.dstAccessMask = vk::AccessFlags();  // NONE
            lastToExternal.dependencyFlags = vk::DependencyFlags();
            dependencies.push_back(lastToExternal);
        }

        // 4. RenderPassCreateInfo 설정
        vk::RenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
        renderPassInfo.pSubpasses = subpasses.data();
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.empty() ? nullptr : dependencies.data();

        // 5. VkRenderPass 생성
        vk::RenderPass renderpass;
        vk::Result result = device_.createRenderPass(&renderPassInfo, nullptr, &renderpass);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to create vk::RenderPass!");
        }

        VulkanRenderPass vrender_pass;

        // 클리어 값 설정
        std::vector<vk::ClearValue> clear_values;
        for (const auto& color : config.clear_colors) {
            vk::ClearColorValue clear_color =
                vk::ClearColorValue(std::array<float, 4>{color[0], color[1], color[2], color[3]});
            vrender_pass.clear_values.emplace_back(clear_color);
        }

        if (has_depth) {
            vk::ClearDepthStencilValue clear_depth = {};
            clear_depth.depth = config.clear_depth_value;
            clear_depth.stencil = config.clear_stencil_value;
            vrender_pass.clear_values.emplace_back(clear_depth);
        }

        vrender_pass.renderpass = renderpass;
        vrender_pass.ref_count = 1;
        vrender_pass.desc_hash = hash_key;  // Store the hash

        // Store the render pass
        renderpasses_.emplace(handle, vrender_pass);

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
#if defined(VULKAN_DEBUG_VALIDATION)
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
#else
        return false;
#endif
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
        pipeline_layout_info.pushConstantRangeCount = 0;
        pipeline_layout_info.pPushConstantRanges = nullptr;

        try {
            pipeline_layout_ = device_.createPipelineLayout(pipeline_layout_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create pipeline layout: ") + e.what());
        }
    }

    void CreateDescriptorPool() {
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
        // 버퍼와 텍스처 핸들을 사용하여 디스크립터 셋을 업데이트
        // 단, 버퍼 핸들이 0일 경우 해당 바인딩을 무시

        std::vector<vk::WriteDescriptorSet> descriptor_writes;

        if (buffer_handle.id != 0) {
            auto buffer_it = buffers_.find(buffer_handle);
            if (buffer_it == buffers_.end()) {
                throw std::runtime_error("Invalid BufferHandle provided to UpdateDescriptorSet.");
            }

            vk::DescriptorBufferInfo buffer_info{};
            buffer_info.buffer = buffer_it->second.buffer;
            buffer_info.offset = 0;
            buffer_info.range = VK_WHOLE_SIZE;

            vk::WriteDescriptorSet write{};
            write.dstSet = ds.descriptor_set;
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eUniformBuffer;
            write.descriptorCount = 1;
            write.pBufferInfo = &buffer_info;

            descriptor_writes.push_back(write);
        }

        if (texture_handle.id != 0) {
            auto texture_it = textures_.find(texture_handle);
            if (texture_it == textures_.end()) {
                throw std::runtime_error("Invalid TextureHandle provided to UpdateDescriptorSet.");
            }

            auto sampler_it = samplers_.find(texture_it->second.sampler_handle);
            if (sampler_it == samplers_.end()) {
                throw std::runtime_error("Invalid SamplerHandle in TextureHandle.");
            }

            vk::DescriptorImageInfo image_info{};
            image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            image_info.imageView = texture_it->second.image_view;
            image_info.sampler = sampler_it->second.sampler;

            vk::WriteDescriptorSet write{};
            write.dstSet = ds.descriptor_set;
            write.dstBinding = 1;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &image_info;

            descriptor_writes.push_back(write);
        }

        if (!descriptor_writes.empty()) {
            device_.updateDescriptorSets(descriptor_writes, {});
        }
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
        auto it = renderpasses_.find(handle);
        if (it != renderpasses_.end()) {
            if (--it->second.ref_count == 0) {
                device_.destroyRenderPass(it->second.renderpass);
                renderpasses_.erase(it);
            }
        }
    }

    vk::CompareOp Convert(CompareOp op) {
        switch (op) {
            case CompareOp::kNever:
                return vk::CompareOp::eNever;
            case CompareOp::kLess:
                return vk::CompareOp::eLess;
            case CompareOp::kEqual:
                return vk::CompareOp::eEqual;
            case CompareOp::kLessOrEqual:
                return vk::CompareOp::eLessOrEqual;
            case CompareOp::kGreater:
                return vk::CompareOp::eGreater;
            case CompareOp::kNotEqual:
                return vk::CompareOp::eNotEqual;
            case CompareOp::kGreaterOrEqual:
                return vk::CompareOp::eGreaterOrEqual;
            case CompareOp::kAlways:
                return vk::CompareOp::eAlways;
            default:
                return vk::CompareOp::eNever;
        }
    }

    vk::BlendFactor Convert(BlendFactor factor) {
        switch (factor) {
            case BlendFactor::kZero:
                return vk::BlendFactor::eZero;
            case BlendFactor::kOne:
                return vk::BlendFactor::eOne;
            case BlendFactor::kSrcColor:
                return vk::BlendFactor::eSrcColor;
            case BlendFactor::kOneMinusSrcColor:
                return vk::BlendFactor::eOneMinusSrcColor;
            case BlendFactor::kDstColor:
                return vk::BlendFactor::eDstColor;
            case BlendFactor::kOneMinusDstColor:
                return vk::BlendFactor::eOneMinusDstColor;
            case BlendFactor::kSrcAlpha:
                return vk::BlendFactor::eSrcAlpha;
            case BlendFactor::kOneMinusSrcAlpha:
                return vk::BlendFactor::eOneMinusSrcAlpha;
            case BlendFactor::kDstAlpha:
                return vk::BlendFactor::eDstAlpha;
            case BlendFactor::kOneMinusDstAlpha:
                return vk::BlendFactor::eOneMinusDstAlpha;
            case BlendFactor::kConstantColor:
                return vk::BlendFactor::eConstantColor;
            case BlendFactor::kOneMinusConstantColor:
                return vk::BlendFactor::eOneMinusConstantColor;
            case BlendFactor::kConstantAlpha:
                return vk::BlendFactor::eConstantAlpha;
            case BlendFactor::kOneMinusConstantAlpha:
                return vk::BlendFactor::eOneMinusConstantAlpha;
            case BlendFactor::kSrcAlphaSaturate:
                return vk::BlendFactor::eSrcAlphaSaturate;
            default:
                return vk::BlendFactor::eZero;
        }
    }

    vk::BlendOp Convert(BlendOp op) {
        switch (op) {
            case BlendOp::kAdd:
                return vk::BlendOp::eAdd;
            case BlendOp::kSubtract:
                return vk::BlendOp::eSubtract;
            case BlendOp::kReverseSubtract:
                return vk::BlendOp::eReverseSubtract;
            case BlendOp::kMin:
                return vk::BlendOp::eMin;
            case BlendOp::kMax:
                return vk::BlendOp::eMax;
            default:
                return vk::BlendOp::eAdd;
        }
    }

    Format Convert(vk::Format format) {
        switch (format) {
            case vk::Format::eR8G8B8A8Srgb:
                return Format::kR8G8B8A8Srgb;
            case vk::Format::eB8G8R8A8Unorm:
                return Format::kB8G8R8A8Unorm;
            case vk::Format::eD16Unorm:
                return Format::kD16Unorm;
            case vk::Format::eX8D24UnormPack32:
                return Format::kX8D24UnormPack32;
            case vk::Format::eD32Sfloat:
                return Format::kD32Sfloat;
            case vk::Format::eS8Uint:
                return Format::kS8Uint;
            case vk::Format::eD16UnormS8Uint:
                return Format::kD16UnormS8Uint;
            case vk::Format::eD24UnormS8Uint:
                return Format::kD24UnormS8Uint;
            case vk::Format::eD32SfloatS8Uint:
                return Format::kD32SfloatS8Uint;
            default:
                throw std::invalid_argument("Unsupported format");
        }
    }

    vk::Format Convert(Format format) {
        switch (format) {
            case Format::kUndefined:
                return vk::Format::eUndefined;
            case Format::kR8G8B8Srgb:
                return vk::Format::eR8G8B8Srgb;
            case Format::kB8G8R8Unorm:
                return vk::Format::eB8G8R8Unorm;
            case Format::kB8G8R8Snorm:
                return vk::Format::eB8G8R8Snorm;
            case Format::kB8G8R8Uscaled:
                return vk::Format::eB8G8R8Uscaled;
            case Format::kB8G8R8Sscaled:
                return vk::Format::eB8G8R8Sscaled;
            case Format::kB8G8R8Uint:
                return vk::Format::eB8G8R8Uint;
            case Format::kB8G8R8Sint:
                return vk::Format::eB8G8R8Sint;
            case Format::kB8G8R8Srgb:
                return vk::Format::eB8G8R8Srgb;
            case Format::kR8G8B8A8Unorm:
                return vk::Format::eR8G8B8A8Unorm;
            case Format::kR8G8B8A8Snorm:
                return vk::Format::eR8G8B8A8Snorm;
            case Format::kR8G8B8A8Uscaled:
                return vk::Format::eR8G8B8A8Uscaled;
            case Format::kR8G8B8A8Sscaled:
                return vk::Format::eR8G8B8A8Sscaled;
            case Format::kR8G8B8A8Uint:
                return vk::Format::eR8G8B8A8Uint;
            case Format::kR8G8B8A8Sint:
                return vk::Format::eR8G8B8A8Sint;
            case Format::kR8G8B8A8Srgb:
                return vk::Format::eR8G8B8A8Srgb;
            case Format::kB8G8R8A8Unorm:
                return vk::Format::eB8G8R8A8Unorm;
            case Format::kB8G8R8A8Snorm:
                return vk::Format::eB8G8R8A8Snorm;
            case Format::kB8G8R8A8Uscaled:
                return vk::Format::eB8G8R8A8Uscaled;
            case Format::kB8G8R8A8Sscaled:
                return vk::Format::eB8G8R8A8Sscaled;
            case Format::kB8G8R8A8Uint:
                return vk::Format::eB8G8R8A8Uint;
            case Format::kB8G8R8A8Sint:
                return vk::Format::eB8G8R8A8Sint;
            case Format::kB8G8R8A8Srgb:
                return vk::Format::eB8G8R8A8Srgb;
            case Format::kA8B8G8R8UnormPack32:
                return vk::Format::eA8B8G8R8UnormPack32;
            case Format::kA8B8G8R8SnormPack32:
                return vk::Format::eA8B8G8R8SnormPack32;
            case Format::kA8B8G8R8UscaledPack32:
                return vk::Format::eA8B8G8R8UscaledPack32;
            case Format::kA8B8G8R8SscaledPack32:
                return vk::Format::eA8B8G8R8SscaledPack32;
            case Format::kA8B8G8R8UintPack32:
                return vk::Format::eA8B8G8R8UintPack32;
            case Format::kA8B8G8R8SintPack32:
                return vk::Format::eA8B8G8R8SintPack32;
            case Format::kA8B8G8R8SrgbPack32:
                return vk::Format::eA8B8G8R8SrgbPack32;
            case Format::kR16Unorm:
                return vk::Format::eR16Unorm;
            case Format::kR16Snorm:
                return vk::Format::eR16Snorm;
            case Format::kR16Uscaled:
                return vk::Format::eR16Uscaled;
            case Format::kR16Sscaled:
                return vk::Format::eR16Sscaled;
            case Format::kR16Uint:
                return vk::Format::eR16Uint;
            case Format::kR16Sint:
                return vk::Format::eR16Sint;
            case Format::kR16Sfloat:
                return vk::Format::eR16Sfloat;
            case Format::kR16G16Unorm:
                return vk::Format::eR16G16Unorm;
            case Format::kR16G16Snorm:
                return vk::Format::eR16G16Snorm;
            case Format::kR16G16Uscaled:
                return vk::Format::eR16G16Uscaled;
            case Format::kR16G16Sscaled:
                return vk::Format::eR16G16Sscaled;
            case Format::kR16G16Uint:
                return vk::Format::eR16G16Uint;
            case Format::kR16G16Sint:
                return vk::Format::eR16G16Sint;
            case Format::kR16G16Sfloat:
                return vk::Format::eR16G16Sfloat;
            case Format::kR16G16B16Unorm:
                return vk::Format::eR16G16B16Unorm;
            case Format::kR16G16B16Snorm:
                return vk::Format::eR16G16B16Snorm;
            case Format::kR16G16B16Uscaled:
                return vk::Format::eR16G16B16Uscaled;
            case Format::kR16G16B16Sscaled:
                return vk::Format::eR16G16B16Sscaled;
            case Format::kR16G16B16Uint:
                return vk::Format::eR16G16B16Uint;
            case Format::kR16G16B16Sint:
                return vk::Format::eR16G16B16Sint;
            case Format::kR16G16B16Sfloat:
                return vk::Format::eR16G16B16Sfloat;
            case Format::kR16G16B16A16Unorm:
                return vk::Format::eR16G16B16A16Unorm;
            case Format::kR16G16B16A16Snorm:
                return vk::Format::eR16G16B16A16Snorm;
            case Format::kR16G16B16A16Uscaled:
                return vk::Format::eR16G16B16A16Uscaled;
            case Format::kR16G16B16A16Sscaled:
                return vk::Format::eR16G16B16A16Sscaled;
            case Format::kR16G16B16A16Uint:
                return vk::Format::eR16G16B16A16Uint;
            case Format::kR16G16B16A16Sint:
                return vk::Format::eR16G16B16A16Sint;
            case Format::kR16G16B16A16Sfloat:
                return vk::Format::eR16G16B16A16Sfloat;
            case Format::kR32Uint:
                return vk::Format::eR32Uint;
            case Format::kR32Sint:
                return vk::Format::eR32Sint;
            case Format::kR32Sfloat:
                return vk::Format::eR32Sfloat;
            case Format::kR32G32Uint:
                return vk::Format::eR32G32Uint;
            case Format::kR32G32Sint:
                return vk::Format::eR32G32Sint;
            case Format::kR32G32Sfloat:
                return vk::Format::eR32G32Sfloat;
            case Format::kR32G32B32Uint:
                return vk::Format::eR32G32B32Uint;
            case Format::kR32G32B32Sint:
                return vk::Format::eR32G32B32Sint;
            case Format::kR32G32B32Sfloat:
                return vk::Format::eR32G32B32Sfloat;
            case Format::kR32G32B32A32Uint:
                return vk::Format::eR32G32B32A32Uint;
            case Format::kR32G32B32A32Sint:
                return vk::Format::eR32G32B32A32Sint;
            case Format::kR32G32B32A32Sfloat:
                return vk::Format::eR32G32B32A32Sfloat;
            case Format::kD16Unorm:
                return vk::Format::eD16Unorm;
            case Format::kX8D24UnormPack32:
                return vk::Format::eX8D24UnormPack32;
            case Format::kD32Sfloat:
                return vk::Format::eD32Sfloat;
            case Format::kS8Uint:
                return vk::Format::eS8Uint;
            case Format::kD16UnormS8Uint:
                return vk::Format::eD16UnormS8Uint;
            case Format::kD24UnormS8Uint:
                return vk::Format::eD24UnormS8Uint;
            case Format::kD32SfloatS8Uint:
                return vk::Format::eD32SfloatS8Uint;
            default:
                throw std::invalid_argument("Unsupported format");
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

    // Converts BufferUsage to Vulkan's BufferUsageFlags
    vk::BufferUsageFlags Convert(BufferUsage usage) {
        vk::BufferUsageFlags vk_usage = {};

        if (usage & BufferUsage::kVertex)
            vk_usage |= vk::BufferUsageFlagBits::eVertexBuffer;
        if (usage & BufferUsage::kIndex)
            vk_usage |= vk::BufferUsageFlagBits::eIndexBuffer;
        if (usage & BufferUsage::kUniform)
            vk_usage |= vk::BufferUsageFlagBits::eUniformBuffer;
        if (usage & BufferUsage::kStorage)
            vk_usage |= vk::BufferUsageFlagBits::eStorageBuffer;
        if (usage & BufferUsage::kIndirect)
            vk_usage |= vk::BufferUsageFlagBits::eIndirectBuffer;

        return vk_usage;
    }

    vk::AttachmentLoadOp Convert(AttachmentLoadOp op) {
        switch (op) {
            case AttachmentLoadOp::kClear:
                return vk::AttachmentLoadOp::eClear;
            case AttachmentLoadOp::kLoad:
                return vk::AttachmentLoadOp::eLoad;
            case AttachmentLoadOp::kDontCare:
                return vk::AttachmentLoadOp::eDontCare;
            default:
                return vk::AttachmentLoadOp::eClear;
        }
    };

    vk::AttachmentStoreOp Convert(AttachmentStoreOp op) {
        switch (op) {
            case AttachmentStoreOp::kStore:
                return vk::AttachmentStoreOp::eStore;
            case AttachmentStoreOp::kDontCare:
                return vk::AttachmentStoreOp::eDontCare;
            default:
                return vk::AttachmentStoreOp::eStore;
        }
    };

    // Converts ShaderStage enum to vk::ShaderStageFlags
    vk::ShaderStageFlags Convert(ShaderStage stage) {
        switch (stage) {
            case ShaderStage::kVertex:
                return vk::ShaderStageFlagBits::eVertex;
            case ShaderStage::kFragment:
                return vk::ShaderStageFlagBits::eFragment;
            case ShaderStage::kCompute:
                return vk::ShaderStageFlagBits::eCompute;
            // Add other shader stages as needed
            default:
                return vk::ShaderStageFlagBits::eVertex;
        }
    }

    vk::SampleCountFlagBits Convert(SampleCount sample) {
        switch (sample) {
            case SampleCount::k1:
                return vk::SampleCountFlagBits::e1;
            case SampleCount::k2:
                return vk::SampleCountFlagBits::e2;
            case SampleCount::k4:
                return vk::SampleCountFlagBits::e4;

            case SampleCount::k8:
                return vk::SampleCountFlagBits::e8;
            case SampleCount::k16:
                return vk::SampleCountFlagBits::e16;
            case SampleCount::k32:
                return vk::SampleCountFlagBits::e32;
            case SampleCount::k64:
                return vk::SampleCountFlagBits::e64;
            default:
                return vk::SampleCountFlagBits::e1;
        }
    }

    // Determines Vulkan Format based on SPIRV-Cross SPIRType
    vk::Format Convert(const spirv_cross::SPIRType& type) {
        if (type.basetype == spirv_cross::SPIRType::Float) {
            switch (type.vecsize) {
                case 1:
                    return vk::Format::eR32Sfloat;
                case 2:
                    return vk::Format::eR32G32Sfloat;
                case 3:
                    return vk::Format::eR32G32B32Sfloat;
                case 4:
                    return vk::Format::eR32G32B32A32Sfloat;
                default:
                    throw std::runtime_error("Unsupported SPIRType vecsize for float.");
            }
        }
        // Handle other base types (Int, UInt, etc.) as needed
        throw std::runtime_error("Unsupported SPIRType basetype for reflection.");
    }

    vk::Format FindSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling,
                                   vk::FormatFeatureFlags features) {
        for (vk::Format format : candidates) {
            vk::FormatProperties props = physical_device_.getFormatProperties(format);

            if ((tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) ||
                (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features)) {
                return format;
            }
        }
        throw std::runtime_error("No compatible format found.");
    }

    vk::Format FindDepthFormat(vk::Format request_format) {
        std::vector<vk::Format> preferred_formats = {request_format, vk::Format::eD32SfloatS8Uint,
                                                     vk::Format::eD24UnormS8Uint, vk::Format::eD16Unorm};

        return FindSupportedFormat(preferred_formats, vk::ImageTiling::eOptimal,
                                   vk::FormatFeatureFlagBits::eDepthStencilAttachment);
    }

    uint64_t HashDesc(const std::vector<Format>& color_formats, const Format depth_format,
                      const RenderPassConfig& config) {
        XXH64_reset(hash_state_, 0);

        for (const auto& format : color_formats) {
            XXH64_update(hash_state_, &format, sizeof(format));
        }

        XXH64_update(hash_state_, &depth_format, sizeof(depth_format));

        for (const auto& clear_color : config.clear_colors) {
            XXH64_update(hash_state_, clear_color.data(), clear_color.size() * sizeof(float));
        }

        XXH64_update(hash_state_, &config.clear_depth, sizeof(config.clear_depth));
        XXH64_update(hash_state_, &config.clear_depth_value, sizeof(config.clear_depth_value));
        XXH64_update(hash_state_, &config.clear_stencil_value, sizeof(config.clear_stencil_value));

        for (const auto& color_op : config.color_attachment_options) {
            XXH64_update(hash_state_, &color_op.load_op, sizeof(color_op.load_op));
            XXH64_update(hash_state_, &color_op.store_op, sizeof(color_op.store_op));
        }

        XXH64_update(hash_state_, &config.depth_attachment_options.load_op,
                     sizeof(config.depth_attachment_options.load_op));
        XXH64_update(hash_state_, &config.depth_attachment_options.store_op,
                     sizeof(config.depth_attachment_options.store_op));

        for (const auto& subpass : config.subpasses) {
            for (const auto& color_attachment : subpass.color_attachments) {
                XXH64_update(hash_state_, &color_attachment, sizeof(color_attachment));
            }
            if (subpass.depth_attachment.has_value()) {
                XXH64_update(hash_state_, &subpass.depth_attachment.value(), sizeof(subpass.depth_attachment.value()));
            }
        }

        uint64_t hash = XXH64_digest(hash_state_);
        return hash;
    }

    // Converts a vector of DescriptorSetLayoutBindings to vk::DescriptorSetLayoutCreateInfo
    vk::DescriptorSetLayout CreateDescriptorSetLayout(const VulkanShader& shader) {
        std::vector<vk::DescriptorSetLayoutBinding> all_bindings;

        // Combine all descriptor bindings
        all_bindings.insert(all_bindings.end(), shader.descriptor_set_layout_bindings.begin(),
                            shader.descriptor_set_layout_bindings.end());
        all_bindings.insert(all_bindings.end(), shader.storage_buffer_bindings.begin(),
                            shader.storage_buffer_bindings.end());
        all_bindings.insert(all_bindings.end(), shader.sampler_bindings.begin(), shader.sampler_bindings.end());

        vk::DescriptorSetLayoutCreateInfo layout_info{};
        layout_info.bindingCount = static_cast<uint32_t>(all_bindings.size());
        layout_info.pBindings = all_bindings.data();

        try {
            return device_.createDescriptorSetLayout(layout_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create descriptor set layout: ") + e.what());
        }
    }

    vk::PipelineLayout CreatePipelineLayout(const VulkanShader& shader) {
        // Create Descriptor Set Layout
        vk::DescriptorSetLayout descriptor_set_layout = CreateDescriptorSetLayout(shader);

        // Collect Push Constant Ranges
        std::vector<vk::PushConstantRange> push_constant_ranges = shader.push_constant_ranges;

        // Create Pipeline Layout
        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;  // Assuming single descriptor set
        pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
        pipeline_layout_info.pushConstantRangeCount = static_cast<uint32_t>(push_constant_ranges.size());
        pipeline_layout_info.pPushConstantRanges = push_constant_ranges.data();

        try {
            return device_.createPipelineLayout(pipeline_layout_info);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to create pipeline layout: ") + e.what());
        }
    }

    // Gets the byte size of a given Vulkan Format
    uint32_t GetFormatSize(vk::Format format) {
        switch (format) {
            case vk::Format::eR32Sfloat:
                return 4;
            case vk::Format::eR32G32Sfloat:
                return 8;
            case vk::Format::eR32G32B32Sfloat:
                return 12;
            case vk::Format::eR32G32B32A32Sfloat:
                return 16;
            // Add additional format sizes as needed
            default:
                throw std::runtime_error("Unsupported Format for size calculation.");
        }
    }
};

// Factory method
std::unique_ptr<IRenderer> IRenderer::Create() { return std::make_unique<VulkanRenderer>(); }

}  // namespace lumora
