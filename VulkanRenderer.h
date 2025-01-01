#ifndef LUMORA_VULKANRENDERER_H_
#define LUMORA_VULKANRENDERER_H_

#include "IRenderer.h"
#include <vulkan/vulkan.hpp>
// #include <vk_mem_alloc.h> // VMA 사용 시

#include <vector>
#include <string>
#include <unordered_map>

namespace Lumora
{

    class VulkanRenderer : public IRenderer
    {
    public:
        VulkanRenderer();
        ~VulkanRenderer() override;

        // IRenderer 구현
        // ----- 버퍼 -----
        BufferHandle CreateBuffer(const BufferDesc &desc) override;
        void UpdateBuffer(BufferHandle handle, const void *data, size_t size) override;
        void BindBuffer(BufferHandle handle, uint32_t bind_point) override;

        // ----- 텍스처 -----
        TextureHandle CreateTexture(const TextureDesc &desc,
                                    const void *initial_data = nullptr) override;
        void BindTexture(TextureHandle handle, uint32_t bind_point) override;

        // ----- 샘플러 -----
        SamplerHandle CreateSampler(const SamplerDesc &desc) override;
        void BindSampler(SamplerHandle handle, uint32_t bind_point) override;

        // ----- 셰이더 -----
        ShaderHandle CreateShader(const ShaderDesc &desc) override;
        void ReleaseShader(ShaderHandle handle) override;

        // ----- 파이프라인 -----
        PipelineHandle CreatePipeline(const PipelineDesc &desc) override;
        void BindPipeline(PipelineHandle handle) override;

        // ----- 리소스 해제 -----
        void ReleaseResource(uint64_t handle) override;

        // ----- 프레임 제어 -----
        void BeginFrame() override;
        void EndFrame() override;

    private:
        // 내부 구조체
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

        struct VulkanShader
        {
            vk::ShaderModule shader_module;
        };

        struct VulkanPipeline
        {
            vk::Pipeline pipeline;
            vk::PipelineLayout pipeline_layout;
        };

        // 리소스 컨테이너
        std::vector<VulkanBuffer> buffers_;
        std::vector<VulkanTexture> textures_;
        std::vector<VulkanSampler> samplers_;
        std::vector<VulkanPipeline> pipelines_;

        // 셰이더: 핸들 → ShaderModule
        //   - 일반 리소스처럼 vector로 관리해도 되지만, 여기서는 map 사용 예시
        std::unordered_map<ShaderHandle, VulkanShader> shaders_;

        // 핸들 생성용 증가 ID
        BufferHandle next_buffer_handle_ = 1;
        TextureHandle next_texture_handle_ = 1;
        SamplerHandle next_sampler_handle_ = 1;
        PipelineHandle next_pipeline_handle_ = 1;
        ShaderHandle next_shader_handle_ = 1;

        // Vulkan
        vk::Instance instance_;
        vk::PhysicalDevice physical_device_;
        vk::Device device_;
        vk::Queue graphics_queue_;
        uint32_t graphics_queue_index_ = 0;

        // 스왑체인, 렌더패스, 프레임버퍼 등
        vk::SurfaceKHR surface_;
        vk::SwapchainKHR swapchain_;
        std::vector<vk::Image> swapchain_images_;
        std::vector<vk::ImageView> swapchain_image_views_;
        vk::Format swapchain_format_ = vk::Format::eB8G8R8A8Unorm;
        vk::Extent2D swapchain_extent_{1280, 720};
        vk::RenderPass render_pass_;
        std::vector<vk::Framebuffer> framebuffers_;

        // 커맨드 풀 / 버퍼
        vk::CommandPool command_pool_;
        std::vector<vk::CommandBuffer> command_buffers_;

        // 동기화
        std::vector<vk::Semaphore> image_available_semaphores_;
        std::vector<vk::Semaphore> render_finished_semaphores_;
        std::vector<vk::Fence> in_flight_fences_;
        uint32_t current_frame_ = 0;
        const int kMaxFramesInFlight = 2;

        // Descriptor (유니폼버퍼 + 텍스처/샘플러 등)
        vk::DescriptorPool descriptor_pool_;
        vk::DescriptorSetLayout descriptor_set_layout_;

        // 내부 함수
        void InitVulkan();
        void CleanupVulkan();
        void CreateSurface();
        void CreateSwapchain();
        void CreateRenderPass();
        void CreateFramebuffers();
        void CreateCommandPoolAndBuffers();
        void CreateSyncObjects();
        void CreateDescriptorPool();
        void CreateDescriptorSetLayout();

        // 버퍼 생성
        BufferHandle CreateBufferInternal(const BufferDesc &desc);

        // 셰이더 로딩
        vk::ShaderModule CreateShaderModule(const std::vector<char> &code);
        std::vector<char> ReadFile(const std::string &filename);

        // 커맨드 버퍼에 기록하는 예시
        void RecordCommandBuffer(vk::CommandBuffer cmd, uint32_t image_index);
    };

} // namespace Lumora

#endif // LUMORA_VULKANRENDERER_H_
