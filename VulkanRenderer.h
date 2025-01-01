#ifndef LUMORA_VULKANRENDERER_H_
#define LUMORA_VULKANRENDERER_H_

#include "IRenderer.h"
#include <vulkan/vulkan.hpp>
// VMA를 사용하려면 아래 헤더가 필요하지만 예제에서는 생략
// #include <vk_mem_alloc.h>

#include <vector>

namespace Lumora
{

    class VulkanRenderer : public IRenderer
    {
    public:
        VulkanRenderer();
        ~VulkanRenderer() override;

        // IRenderer 인터페이스 구현
        BufferHandle CreateBuffer(const BufferDesc &desc) override;
        void UpdateBuffer(BufferHandle handle, const void *data,
                          size_t size) override;
        void BindBuffer(BufferHandle handle, uint32_t bind_point) override;

        TextureHandle CreateTexture(const TextureDesc &desc,
                                    const void *initial_data = nullptr) override;
        void BindTexture(TextureHandle handle, uint32_t bind_point) override;

        SamplerHandle CreateSampler(const SamplerDesc &desc) override;
        void BindSampler(SamplerHandle handle, uint32_t bind_point) override;

        PipelineHandle CreatePipeline(const PipelineDesc &desc) override;
        void BindPipeline(PipelineHandle handle) override;

        void ReleaseResource(uint64_t handle) override;

    private:
        // 내부 리소스 구조체
        struct VulkanBuffer
        {
            vk::Buffer buffer;
            size_t size_in_bytes = 0;
            // VmaAllocation allocation;
            // VmaAllocationInfo alloc_info;
        };

        struct VulkanTexture
        {
            vk::Image image;
            vk::ImageView image_view;
            uint32_t width = 0;
            uint32_t height = 0;
            // VmaAllocation allocation;
        };

        struct VulkanSampler
        {
            vk::Sampler sampler;
        };

        struct VulkanPipeline
        {
            vk::Pipeline pipeline;
            // vk::PipelineLayout pipeline_layout; // 파이프라인 레이아웃 등
        };

        // 핸들 -> 내부 리소스 매핑
        std::vector<VulkanBuffer> buffers_;
        std::vector<VulkanTexture> textures_;
        std::vector<VulkanSampler> samplers_;
        std::vector<VulkanPipeline> pipelines_;

        // 핸들 생성을 위한 증가 ID
        BufferHandle next_buffer_handle_ = 1;
        TextureHandle next_texture_handle_ = 1;
        SamplerHandle next_sampler_handle_ = 1;
        PipelineHandle next_pipeline_handle_ = 1;

        // Vulkan 관련 멤버
        vk::Instance instance_;
        vk::Device device_;
        vk::PhysicalDevice physical_device_;
        vk::Queue graphics_queue_;
        uint32_t graphics_queue_index_ = 0;
        // VmaAllocator allocator_;

        // 내부 헬퍼
        BufferHandle CreateBufferInternal(const BufferDesc &desc);
        void InitVulkan();
        void CleanupVulkan();
    };

} // namespace Lumora

#endif // LUMORA_VULKANRENDERER_H_
