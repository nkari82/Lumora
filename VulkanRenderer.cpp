#include "VulkanRenderer.h"

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

    void VulkanRenderer::InitVulkan()
    {
        // vk::Instance 생성
        vk::ApplicationInfo app_info;
        app_info.pApplicationName = "Lumora App";
        app_info.applicationVersion = 1;
        app_info.pEngineName = "Lumora Engine";
        app_info.engineVersion = 1;
        app_info.apiVersion = VK_API_VERSION_1_2;

        vk::InstanceCreateInfo instance_create_info;
        instance_create_info.pApplicationInfo = &app_info;

        instance_ = vk::createInstance(instance_create_info);

        // 물리 디바이스 선택
        std::vector<vk::PhysicalDevice> physical_devices =
            instance_.enumeratePhysicalDevices();
        if (physical_devices.empty())
        {
            throw std::runtime_error("No Vulkan physical device found!");
        }
        physical_device_ = physical_devices[0];

        // 그래픽스 큐를 지원하는 큐 패밀리 인덱스 찾기
        std::vector<vk::QueueFamilyProperties> queue_families =
            physical_device_.getQueueFamilyProperties();
        for (uint32_t i = 0; i < queue_families.size(); ++i)
        {
            if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics)
            {
                graphics_queue_index_ = i;
                break;
            }
        }

        float queue_priority = 1.0f;
        vk::DeviceQueueCreateInfo queue_create_info;
        queue_create_info.queueFamilyIndex = graphics_queue_index_;
        queue_create_info.queueCount = 1;
        queue_create_info.pQueuePriorities = &queue_priority;

        vk::DeviceCreateInfo device_create_info;
        device_create_info.queueCreateInfoCount = 1;
        device_create_info.pQueueCreateInfos = &queue_create_info;

        device_ = physical_device_.createDevice(device_create_info);
        graphics_queue_ = device_.getQueue(graphics_queue_index_, 0);

        // VMA Allocator 생성 (생략)
        // VmaAllocatorCreateInfo allocator_info = {};
        // allocator_info.physicalDevice = physical_device_;
        // allocator_info.device = device_;
        // allocator_info.instance = instance_;
        // vmaCreateAllocator(&allocator_info, &allocator_);
    }

    void VulkanRenderer::CleanupVulkan()
    {
        // 리소스가 모두 ReleaseResource로 해제되었다고 가정
        // 남은 Vulkan 객체들 해제
        if (device_)
        {
            device_.waitIdle();
            device_.destroy();
        }
        if (instance_)
        {
            instance_.destroy();
        }
    }

    BufferHandle VulkanRenderer::CreateBufferInternal(const BufferDesc &desc)
    {
        // vk::BufferCreateInfo
        vk::BufferCreateInfo buffer_info;
        buffer_info.size = desc.size_in_bytes;

        vk::BufferUsageFlags usage_flags;
        if (desc.usage_uniform_buffer)
        {
            usage_flags |= vk::BufferUsageFlagBits::eUniformBuffer;
        }
        if (desc.usage_vertex_buffer)
        {
            usage_flags |= vk::BufferUsageFlagBits::eVertexBuffer;
        }
        if (desc.usage_index_buffer)
        {
            usage_flags |= vk::BufferUsageFlagBits::eIndexBuffer;
        }
        if (desc.usage_transfer_src)
        {
            usage_flags |= vk::BufferUsageFlagBits::eTransferSrc;
        }
        if (desc.usage_transfer_dst)
        {
            usage_flags |= vk::BufferUsageFlagBits::eTransferDst;
        }
        buffer_info.usage = usage_flags;

        VulkanBuffer vulkan_buffer;
        vulkan_buffer.buffer = device_.createBuffer(buffer_info);
        vulkan_buffer.size_in_bytes = desc.size_in_bytes;

        // VMA 할당 예시 (생략)
        // VmaAllocationCreateInfo alloc_info = {};
        // alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        // vmaAllocateMemoryForBuffer(allocator_, vulkan_buffer.buffer, &alloc_info,
        //                            &vulkan_buffer.allocation, &vulkan_buffer.alloc_info);
        // vmaBindBufferMemory(allocator_, vulkan_buffer.allocation, vulkan_buffer.buffer);

        // 핸들 발급
        BufferHandle handle = next_buffer_handle_++;
        if (handle >= buffers_.size())
        {
            buffers_.resize(handle + 1);
        }
        buffers_[handle] = vulkan_buffer;

        return handle;
    }

    BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc &desc)
    {
        return CreateBufferInternal(desc);
    }

    void VulkanRenderer::UpdateBuffer(BufferHandle handle, const void *data,
                                      size_t size)
    {
        if (handle == 0 || handle >= buffers_.size())
        {
            throw std::runtime_error("Invalid buffer handle in UpdateBuffer.");
        }
        // VMA 사용 시 매핑 후 memcpy
        // VulkanBuffer& buf = buffers_[handle];
        // void* mapped_data = nullptr;
        // vmaMapMemory(allocator_, buf.allocation, &mapped_data);
        // std::memcpy(mapped_data, data, size);
        // vmaUnmapMemory(allocator_, buf.allocation);
    }

    void VulkanRenderer::BindBuffer(BufferHandle handle, uint32_t bind_point)
    {
        // 실제 vkCmdBindVertexBuffers / vkCmdBindIndexBuffer / DescriptorSet 업데이트 등
    }

    TextureHandle VulkanRenderer::CreateTexture(const TextureDesc &desc,
                                                const void *initial_data)
    {
        vk::ImageCreateInfo image_info;
        image_info.imageType = vk::ImageType::e2D;
        image_info.extent.width = desc.width;
        image_info.extent.height = desc.height;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = vk::Format::eR8G8B8A8Unorm;
        image_info.usage =
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

        VulkanTexture vulkan_texture;
        vulkan_texture.image = device_.createImage(image_info);
        vulkan_texture.width = desc.width;
        vulkan_texture.height = desc.height;

        // VMA 할당 & 바인딩 (생략)
        // vmaAllocateMemoryForImage(allocator_, vulkan_texture.image, &alloc_info,
        //                           &vulkan_texture.allocation, nullptr);
        // vmaBindImageMemory(allocator_, vulkan_texture.allocation, vulkan_texture.image);

        vk::ImageViewCreateInfo view_info;
        view_info.image = vulkan_texture.image;
        view_info.viewType = vk::ImageViewType::e2D;
        view_info.format = image_info.format;
        view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;

        vulkan_texture.image_view = device_.createImageView(view_info);

        // initial_data 업로드 작업 / 레이아웃 전환 등 필요

        TextureHandle handle = next_texture_handle_++;
        if (handle >= textures_.size())
        {
            textures_.resize(handle + 1);
        }
        textures_[handle] = vulkan_texture;
        return handle;
    }

    void VulkanRenderer::BindTexture(TextureHandle handle, uint32_t bind_point)
    {
        // DescriptorSet에 vkUpdateDescriptorSets 등
    }

    SamplerHandle VulkanRenderer::CreateSampler(const SamplerDesc &desc)
    {
        vk::SamplerCreateInfo sampler_info;
        sampler_info.magFilter = vk::Filter::eLinear;
        sampler_info.minFilter = vk::Filter::eLinear;
        sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
        sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
        sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;

        VulkanSampler vulkan_sampler;
        vulkan_sampler.sampler = device_.createSampler(sampler_info);

        SamplerHandle handle = next_sampler_handle_++;
        if (handle >= samplers_.size())
        {
            samplers_.resize(handle + 1);
        }
        samplers_[handle] = vulkan_sampler;
        return handle;
    }

    void VulkanRenderer::BindSampler(SamplerHandle handle, uint32_t bind_point)
    {
        // DescriptorSet에 vkUpdateDescriptorSets 등
    }

    PipelineHandle VulkanRenderer::CreatePipeline(const PipelineDesc &desc)
    {
        // 실제 vk::GraphicsPipelineCreateInfo 구성
        // 셰이더 스테이지, Input Assembly, Viewport, Rasterizer, DepthStencil 등
        // 다양한 생성 정보가 필요

        // 예시로 Dummy
        VulkanPipeline pipeline;
        pipeline.pipeline = vk::Pipeline();

        PipelineHandle handle = next_pipeline_handle_++;
        if (handle >= pipelines_.size())
        {
            pipelines_.resize(handle + 1);
        }
        pipelines_[handle] = pipeline;
        return handle;
    }

    void VulkanRenderer::BindPipeline(PipelineHandle handle)
    {
        // vkCmdBindPipeline 호출 등
    }

    void VulkanRenderer::ReleaseResource(uint64_t handle)
    {
        // 어떤 타입의 리소스인지 구분
        // (버퍼, 텍스처, 샘플러, 파이프라인)
        if (handle < buffers_.size() && buffers_[handle].buffer)
        {
            device_.destroyBuffer(buffers_[handle].buffer);
            buffers_[handle].buffer = nullptr;
            return;
        }
        if (handle < textures_.size() && textures_[handle].image)
        {
            device_.destroyImageView(textures_[handle].image_view);
            device_.destroyImage(textures_[handle].image);
            textures_[handle].image = nullptr;
            return;
        }
        if (handle < samplers_.size() && samplers_[handle].sampler)
        {
            device_.destroySampler(samplers_[handle].sampler);
            samplers_[handle].sampler = nullptr;
            return;
        }
        if (handle < pipelines_.size() && pipelines_[handle].pipeline)
        {
            device_.destroyPipeline(pipelines_[handle].pipeline);
            pipelines_[handle].pipeline = nullptr;
            return;
        }
    }

} // namespace Lumora
