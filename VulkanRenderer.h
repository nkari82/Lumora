#ifndef LUMORA_VULKANRENDERER_H_
#define LUMORA_VULKANRENDERER_H_

#include "IRenderer.h"

#include <vulkan/vulkan.hpp>
// VMA 사용하려면 아래 헤더 필요
// #include <vk_mem_alloc.h>

#include <vector>
#include <string>

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

        void BeginFrame() override;
        void EndFrame() override;

    private:
        // 내부 리소스 구조체
        struct VulkanBuffer
        {
            vk::Buffer buffer;
            size_t size_in_bytes = 0;
            // VmaAllocation allocation;
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
            vk::PipelineLayout pipeline_layout;
            // 여러 DescriptorSetLayout 등을 포함
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
        vk::PhysicalDevice physical_device_;
        vk::Device device_;

        vk::Queue graphics_queue_;
        uint32_t graphics_queue_index_ = 0;

        // 스왑체인
        vk::SurfaceKHR surface_; // 실제 window surface
        vk::SwapchainKHR swapchain_;
        std::vector<vk::Image> swapchain_images_;
        std::vector<vk::ImageView> swapchain_image_views_;
        vk::Format swapchain_format_ = vk::Format::eB8G8R8A8Unorm;
        vk::Extent2D swapchain_extent_{1280, 720}; // 예시로 고정

        // 렌더패스 / 프레임버퍼
        vk::RenderPass render_pass_;
        std::vector<vk::Framebuffer> framebuffers_;

        // 커맨드 풀 / 커맨드 버퍼
        vk::CommandPool command_pool_;
        std::vector<vk::CommandBuffer> command_buffers_;

        // 동기화
        std::vector<vk::Semaphore> image_available_semaphores_;
        std::vector<vk::Semaphore> render_finished_semaphores_;
        std::vector<vk::Fence> in_flight_fences_;
        uint32_t current_frame_ = 0;
        const int kMaxFramesInFlight = 2;

        // 기본 DescriptorPool (유니폼버퍼/텍스처용)
        vk::DescriptorPool descriptor_pool_;

        // “유니폼 버퍼 + 샘플러” DescriptorSetLayout
        vk::DescriptorSetLayout descriptor_set_layout_;

        // 리소스 헬퍼
        BufferHandle CreateBufferInternal(const BufferDesc &desc);
        void InitVulkan();
        void CleanupVulkan();

        // Swapchain + 렌더패스 + 프레임버퍼 + 동기화 + 커맨드버퍼 초기화
        void CreateSurface(); // 플랫폼별 구현 필요 (GLFW/SDL/Win32 등)
        void CreateSwapchain();
        void CreateRenderPass();
        void CreateFramebuffers();
        void CreateCommandPoolAndBuffers();
        void CreateSyncObjects();

        void CreateDescriptorPool();
        void CreateDescriptorSetLayout();

        // 파이프라인 생성 헬퍼
        vk::ShaderModule CreateShaderModule(const std::vector<char> &code);
        std::vector<char> ReadFile(const std::string &filename);

        // 실제 커맨드 버퍼에 그리는 예시
        void RecordCommandBuffer(vk::CommandBuffer cmd, uint32_t image_index);
    };

} // namespace Lumora

#endif // LUMORA_VULKANRENDERER_H_
