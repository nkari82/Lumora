#pragma once

#include "IRenderer.h"
#include <vulkan/vulkan.hpp>

// VMA 관련 헤더
// #include <vk_mem_alloc.h>  // 실제론 VMA 헤더가 필요하지만 예시에서는 생략

namespace Lumora
{
    // VulkanRenderer
    class VulkanRenderer : public IRenderer
    {
    public:
        VulkanRenderer();
        virtual ~VulkanRenderer();

        // IRenderer 인터페이스 구현
        BufferHandle CreateBuffer(const BufferDesc &desc) override;
        void UpdateBuffer(BufferHandle handle, const void *data, size_t size) override;
        void BindBuffer(BufferHandle handle, uint32_t bindPoint) override;

        TextureHandle CreateTexture(const TextureDesc &desc, const void *initialData = nullptr) override;
        void BindTexture(TextureHandle handle, uint32_t bindPoint) override;

        SamplerHandle CreateSampler(const SamplerDesc &desc) override;
        void BindSampler(SamplerHandle handle, uint32_t bindPoint) override;

        PipelineHandle CreatePipeline(const PipelineDesc &desc) override;
        void BindPipeline(PipelineHandle handle) override;

        void ReleaseResource(uint64_t handle) override;

    private:
        // 내부에서 리소스 정보를 보관할 구조체들
        struct VulkanBuffer
        {
            vk::Buffer buffer;
            // VMA로 할당했을 경우 VmaAllocation allocation;
            // VmaAllocationInfo allocInfo;
            size_t sizeInBytes = 0;
        };

        struct VulkanTexture
        {
            vk::Image image;
            // VmaAllocation allocation;
            vk::ImageView imageView;
            uint32_t width;
            uint32_t height;
        };

        struct VulkanSampler
        {
            vk::Sampler sampler;
        };

        struct VulkanPipeline
        {
            vk::Pipeline pipeline;
        };

        // 리소스 핸들 -> 실제 객체 매핑
        // 단순화를 위해 std::vector 사용, 실제론 더 복잡한 관리가 필요.
        std::vector<VulkanBuffer> m_buffers;
        std::vector<VulkanTexture> m_textures;
        std::vector<VulkanSampler> m_samplers;
        std::vector<VulkanPipeline> m_pipelines;

        // 핸들을 생성하기 위한 증가 ID
        BufferHandle m_nextBufferHandle = 1;
        TextureHandle m_nextTextureHandle = 1;
        SamplerHandle m_nextSamplerHandle = 1;
        PipelineHandle m_nextPipelineHandle = 1;

        // Vulkan 관련 멤버
        vk::Instance m_instance;
        vk::Device m_device;
        vk::PhysicalDevice m_physicalDevice;
        vk::Queue m_graphicsQueue;
        uint32_t m_graphicsQueueIndex = 0;
        // VMA를 사용하려면 VmaAllocator m_allocator; 등이 필요

        // 내부 헬퍼
        BufferHandle CreateBufferInternal(const BufferDesc &desc);
        void InitVulkan();
        void CleanupVulkan();
    };
}
