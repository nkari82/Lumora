#include "VulkanRenderer.h"

#include <stdexcept>
#include <iostream>

namespace Lumora
{
    // 스테틱 함수 구현 (IRenderer.h에 선언됨)
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
        // 여기에 vk::Instance, vk::Device, vk::PhysicalDevice,
        // VMA Allocator 초기화, 그래픽스 큐 획득 등 구현
        // 간단 예시

        vk::ApplicationInfo appInfo;
        appInfo.pApplicationName = "Lumora App";
        appInfo.applicationVersion = 1;
        appInfo.pEngineName = "Lumora Engine";
        appInfo.engineVersion = 1;
        appInfo.apiVersion = VK_API_VERSION_1_2;

        vk::InstanceCreateInfo createInfo;
        createInfo.pApplicationInfo = &appInfo;

        m_instance = vk::createInstance(createInfo);

        auto physicalDevices = m_instance.enumeratePhysicalDevices();
        if (physicalDevices.empty())
        {
            throw std::runtime_error("Vulkan physical device not found!");
        }
        m_physicalDevice = physicalDevices[0];

        // 큐 패밀리 인덱스 찾기 (그래픽스 지원)
        auto queueFamilies = m_physicalDevice.getQueueFamilyProperties();
        for (uint32_t i = 0; i < queueFamilies.size(); i++)
        {
            if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics)
            {
                m_graphicsQueueIndex = i;
                break;
            }
        }

        float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.queueFamilyIndex = m_graphicsQueueIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        vk::DeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

        m_device = m_physicalDevice.createDevice(deviceCreateInfo);
        m_graphicsQueue = m_device.getQueue(m_graphicsQueueIndex, 0);

        // VMA Allocator 생성 로직 등
        // VmaAllocatorCreateInfo allocatorInfo = {};
        // allocatorInfo.physicalDevice = m_physicalDevice;
        // allocatorInfo.device = m_device;
        // allocatorInfo.instance = m_instance;
        // vmaCreateAllocator(&allocatorInfo, &m_allocator);
    }

    void VulkanRenderer::CleanupVulkan()
    {
        // 모든 리소스가 ReleaseResource로 해제되었다고 가정
        // 남은 Vulkan 객체 해제
        // if (m_allocator) { vmaDestroyAllocator(m_allocator); }

        if (m_device)
        {
            m_device.waitIdle();
            m_device.destroy();
        }
        if (m_instance)
        {
            m_instance.destroy();
        }
    }

    // 내부 헬퍼
    BufferHandle VulkanRenderer::CreateBufferInternal(const BufferDesc &desc)
    {
        // vk::BufferCreateInfo 설정
        vk::BufferCreateInfo bufferInfo;
        bufferInfo.size = desc.sizeInBytes;

        // 사용 용도 매핑
        vk::BufferUsageFlags usage;
        if (desc.usageUniformBuffer)
            usage |= vk::BufferUsageFlagBits::eUniformBuffer;
        if (desc.usageVertexBuffer)
            usage |= vk::BufferUsageFlagBits::eVertexBuffer;
        if (desc.usageIndexBuffer)
            usage |= vk::BufferUsageFlagBits::eIndexBuffer;
        if (desc.usageTransferSrc)
            usage |= vk::BufferUsageFlagBits::eTransferSrc;
        if (desc.usageTransferDst)
            usage |= vk::BufferUsageFlagBits::eTransferDst;
        bufferInfo.usage = usage;

        // 실제 버퍼 생성
        VulkanBuffer vulkanBuffer;
        vulkanBuffer.buffer = m_device.createBuffer(bufferInfo);
        vulkanBuffer.sizeInBytes = desc.sizeInBytes;

        // VMA 할당 로직 (예시)
        // VmaAllocationCreateInfo allocCreateInfo = {};
        // allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        // vmaAllocateMemoryForBuffer(m_allocator, vulkanBuffer.buffer, &allocCreateInfo, &vulkanBuffer.allocation, &vulkanBuffer.allocInfo);
        // vmaBindBufferMemory(m_allocator, vulkanBuffer.allocation, vulkanBuffer.buffer);

        // 핸들 발급
        BufferHandle handle = m_nextBufferHandle++;
        if (handle > m_buffers.size())
        {
            m_buffers.resize(handle + 1);
        }
        m_buffers[handle] = vulkanBuffer;

        return handle;
    }

    // IRenderer 구현부

    BufferHandle VulkanRenderer::CreateBuffer(const BufferDesc &desc)
    {
        return CreateBufferInternal(desc);
    }

    void VulkanRenderer::UpdateBuffer(BufferHandle handle, const void *data, size_t size)
    {
        if (handle == 0 || handle >= m_buffers.size())
        {
            throw std::runtime_error("Invalid buffer handle in UpdateBuffer");
        }
        VulkanBuffer &buf = m_buffers[handle];

        // VMA 사용 시
        // void* mappedData = nullptr;
        // vmaMapMemory(m_allocator, buf.allocation, &mappedData);
        // std::memcpy(mappedData, data, size);
        // vmaUnmapMemory(m_allocator, buf.allocation);

        // 여기서는 단순 예시
        // 실제 Vulkan 메모리 매핑 로직 필요
    }

    void VulkanRenderer::BindBuffer(BufferHandle handle, uint32_t bindPoint)
    {
        // 실제 Vulkan 커맨드 버퍼에 바인딩하기 위한 로직
        // 예: vkCmdBindVertexBuffers / vkCmdBindIndexBuffer / Descriptor 셋 업데이트 등
    }

    TextureHandle VulkanRenderer::CreateTexture(const TextureDesc &desc, const void *initialData)
    {
        // vk::ImageCreateInfo 작성
        vk::ImageCreateInfo imageInfo;
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.extent.width = desc.width;
        imageInfo.extent.height = desc.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR8G8B8A8Unorm; // 예시
        imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

        // 이미지 생성 + VMA 할당
        VulkanTexture tex;
        tex.image = m_device.createImage(imageInfo);
        tex.width = desc.width;
        tex.height = desc.height;

        // VMA 할당 및 Bind
        // VmaAllocationCreateInfo allocInfo = {};
        // allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        // vmaAllocateMemoryForImage(m_allocator, tex.image, &allocInfo, &tex.allocation, nullptr);
        // vmaBindImageMemory(m_allocator, tex.allocation, tex.image);

        // ImageView 생성
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = tex.image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = imageInfo.format;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        tex.imageView = m_device.createImageView(viewInfo);

        // 초기 데이터 업로드, 레이아웃 전환, 커맨드 버퍼 등 필요

        // 핸들 발급
        TextureHandle handle = m_nextTextureHandle++;
        if (handle > m_textures.size())
        {
            m_textures.resize(handle + 1);
        }
        m_textures[handle] = tex;
        return handle;
    }

    void VulkanRenderer::BindTexture(TextureHandle handle, uint32_t bindPoint)
    {
        // DescriptorSet 업데이트 등
    }

    SamplerHandle VulkanRenderer::CreateSampler(const SamplerDesc &desc)
    {
        // vk::SamplerCreateInfo를 desc 기반으로 채우기
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;

        VulkanSampler sampler;
        sampler.sampler = m_device.createSampler(samplerInfo);

        SamplerHandle handle = m_nextSamplerHandle++;
        if (handle > m_samplers.size())
        {
            m_samplers.resize(handle + 1);
        }
        m_samplers[handle] = sampler;

        return handle;
    }

    void VulkanRenderer::BindSampler(SamplerHandle handle, uint32_t bindPoint)
    {
        // DescriptorSet 업데이트 등
    }

    PipelineHandle VulkanRenderer::CreatePipeline(const PipelineDesc &desc)
    {
        // vk::GraphicsPipelineCreateInfo 등 구성
        // 셰이더 로드, Input Assembly, Viewport, Rasterizer, ColorBlend 등등

        vk::Pipeline dummyPipeline; // 실제 구현 시 생성
        VulkanPipeline pipeline;
        pipeline.pipeline = dummyPipeline;

        PipelineHandle handle = m_nextPipelineHandle++;
        if (handle > m_pipelines.size())
        {
            m_pipelines.resize(handle + 1);
        }
        m_pipelines[handle] = pipeline;

        return handle;
    }

    void VulkanRenderer::BindPipeline(PipelineHandle handle)
    {
        // 실제 vkCmdBindPipeline 호출
    }

    void VulkanRenderer::ReleaseResource(uint64_t handle)
    {
        // 버퍼, 텍스처, 샘플러, 파이프라인 중 하나인지 구분하여 해제
        // 간단히 “버퍼 범위인지, 텍스처 범위인지” 등으로 판별
        if (handle < m_buffers.size() && m_buffers[handle].buffer)
        {
            // VMA 사용 시 vmaDestroyBuffer(...)
            m_device.destroyBuffer(m_buffers[handle].buffer);
            m_buffers[handle].buffer = nullptr;
            return;
        }
        if (handle < m_textures.size() && m_textures[handle].image)
        {
            m_device.destroyImageView(m_textures[handle].imageView);
            m_device.destroyImage(m_textures[handle].image);
            m_textures[handle].image = nullptr;
            return;
        }
        if (handle < m_samplers.size() && m_samplers[handle].sampler)
        {
            m_device.destroySampler(m_samplers[handle].sampler);
            m_samplers[handle].sampler = nullptr;
            return;
        }
        if (handle < m_pipelines.size() && m_pipelines[handle].pipeline)
        {
            m_device.destroyPipeline(m_pipelines[handle].pipeline);
            m_pipelines[handle].pipeline = nullptr;
            return;
        }
    }
}
