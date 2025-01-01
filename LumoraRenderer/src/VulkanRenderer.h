#ifndef LUMORA_VULKANRENDERER_H_
#define LUMORA_VULKANRENDERER_H_

#include "../include/IRenderer.h"
#include <vulkan/vulkan.hpp>

#include <vector>
#include <unordered_map>

namespace Lumora
{

    class VulkanRenderer : public IRenderer
    {
    public:
        VulkanRenderer();
        ~VulkanRenderer() override;

        // IRenderer 구현
        SwapChainHandle CreateSwapChain(const SwapChainDesc &desc) override;

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
        // 내부 구조체들
        struct VulkanSwapChain
        {
            vk::SwapchainKHR swapchain;
            vk::Extent2D extent;
            std::vector<vk::Image> images;
            std::vector<vk::ImageView> image_views;
            vk::RenderPass render_pass;
            std::vector<vk::Framebuffer> framebuffers;
        };

        struct VulkanBuffer
        {
            vk::Buffer buffer;
            size_t size_in_bytes;
        };

        struct VulkanTexture
        {
            vk::Image image;
            vk::ImageView image_view;
            uint32_t width, height;
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

        // 컨테이너
        std::vector<VulkanSwapChain> swapchains_;
        std::vector<VulkanBuffer> buffers_;
        std::vector<VulkanTexture> textures_;
        std::vector<VulkanSampler> samplers_;
        std::vector<VulkanPipeline> pipelines_;
        std::unordered_map<ShaderHandle, VulkanShader> shaders_;

        // 핸들 발급
        SwapChainHandle next_swapchain_handle_ = 1;
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

        // 다른 자원들 (커맨드 풀, 동기화 등)
        bool created_ = false;

    private:
        void InitVulkan();
        void CleanupVulkan();

        SwapChainHandle CreateSwapChainInternal(const SwapChainDesc &desc);

        vk::ShaderModule CreateShaderModule(const std::vector<char> &code);
        std::vector<char> ReadFile(const std::string &filename);

        // “앞으로의 과제”:
        // - Win32 Surface 만들기 (vkCreateWin32SurfaceKHR)
        // - 멀티 샘플링, 컴퓨트 파이프라인, 큐브맵, 리사이즈 지원 등
    };

} // namespace Lumora

#endif // LUMORA_VULKANRENDERER_H_
