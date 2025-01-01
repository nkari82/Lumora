#ifndef LUMORA_VULKANRENDERER_H_
#define LUMORA_VULKANRENDERER_H_

#include "IRenderer.h"

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h> // VMA 헤더

#include <vector>
#include <unordered_map>

//
// GLFW (예시) - 실제 플랫폼별 구현 시 대체 가능
//
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// #include <GLFW/glfw3native.h> // (플랫폼별 Surface 생성 시)

namespace Lumora
{

    class VulkanRenderer : public IRenderer
    {
    public:
        VulkanRenderer();
        ~VulkanRenderer() override;

        // IRenderer 구현
        BufferHandle CreateBuffer(const BufferDesc &desc) override;
        void UpdateBuffer(BufferHandle handle, const void *data, size_t size) override;
        void BindBuffer(BufferHandle handle, uint32_t bind_point) override;

        TextureHandle CreateTexture(const TextureDesc &desc,
                                    const void *initial_data) override;
        void BindTexture(TextureHandle handle, uint32_t bind_point) override;

        SamplerHandle CreateSampler(const SamplerDesc &desc) override;
        void BindSampler(SamplerHandle handle, uint32_t bind_point) override;

        ShaderHandle CreateShader(const ShaderDesc &desc) override;
        void ReleaseShader(ShaderHandle handle) override;

        PipelineHandle CreatePipeline(const PipelineDesc &desc) override;
        void BindPipeline(PipelineHandle handle) override;

        void ReleaseResource(uint64_t handle) override;

        void BeginFrame() override;
        void EndFrame() override;

    private:
        // 내부 구조체
        struct VulkanBuffer
        {
            vk::Buffer buffer;
            VmaAllocation allocation = nullptr;
            size_t size_in_bytes = 0;
        };

        struct VulkanTexture
        {
            vk::Image image;
            vk::ImageView image_view;
            VmaAllocation allocation = nullptr;
            uint32_t width = 0;
            uint32_t height = 0;
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

        // 리소스 저장소
        std::vector<VulkanBuffer> buffers_;
        std::vector<VulkanTexture> textures_;
        std::vector<VulkanSampler> samplers_;
        std::vector<VulkanPipeline> pipelines_;
        std::unordered_map<ShaderHandle, VulkanShader> shaders_;

        // 핸들 발급용
        BufferHandle next_buffer_handle_ = 1;
        TextureHandle next_texture_handle_ = 1;
        SamplerHandle next_sampler_handle_ = 1;
        PipelineHandle next_pipeline_handle_ = 1;
        ShaderHandle next_shader_handle_ = 1;

        // Vulkan Core
        vk::Instance instance_;
        vk::PhysicalDevice physical_device_;
        vk::Device device_;
        vk::Queue graphics_queue_;
        uint32_t graphics_queue_index_ = 0;

        // VMA
        VmaAllocator allocator_ = nullptr;

        // GLFW Window + Surface
        GLFWwindow *window_ = nullptr;
        vk::SurfaceKHR surface_;

        // 스왑체인
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

        // Descriptor
        vk::DescriptorPool descriptor_pool_;
        vk::DescriptorSetLayout descriptor_set_layout_;
        std::vector<vk::DescriptorSet> descriptor_sets_; // 하나의 셋(UBO+Texture)만 예시

        // 유니폼버퍼 (예시)
        BufferHandle ubo_handle_ = 0; // MVP용 등

        // 인덱스 버퍼 (삼각형 예시)
        BufferHandle ibo_handle_ = 0;
        uint32_t index_count_ = 0;

        // 정점 버퍼 (삼각형 예시)
        BufferHandle vbo_handle_ = 0;

        // 내부 함수들
        void InitVulkan();
        void CleanupVulkan();

        // GLFW Window
        void InitWindow();
        void CreateSurface();

        void CreateSwapchain();
        void CreateRenderPass();
        void CreateFramebuffers();
        void CreateCommandPoolAndBuffers();
        void CreateSyncObjects();
        void CreateDescriptorPool();
        void CreateDescriptorSetLayout();

        // VMA 초기화
        void InitVma();

        // 커맨드 버퍼 녹화
        void RecordCommandBuffer(vk::CommandBuffer cmd, uint32_t image_index);

        // 헬퍼
        vk::ShaderModule CreateShaderModule(const std::vector<char> &code);
        std::vector<char> ReadFile(const std::string &filename);

        // 스테이징 버퍼를 이용해 GPU 자원에 데이터 업로드
        void UploadDataToBuffer(const void *src_data, size_t src_size,
                                vk::Buffer dst_buffer);

        // “테스트용” 삼각형 인덱스 버퍼 + 정점 버퍼 + UBO 준비
        void CreateTestTriangleResources();
    };

} // namespace Lumora

#endif // LUMORA_VULKANRENDERER_H_
