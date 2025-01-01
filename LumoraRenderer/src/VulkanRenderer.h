#pragma once

#include "../include/IRenderer.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <vulkan/vulkan.hpp>

// #include <vk_mem_alloc.h> // VMA 사용할 경우
// ...

#include <unordered_map>
#include <vector>

namespace Lumora {

class VulkanRenderer : public IRenderer {
   public:
    VulkanRenderer();
    ~VulkanRenderer() override;

    // IRenderer 구현
    // 스왑체인
    SwapChainHandle CreateSwapChain(const SwapChainDesc& desc) override;

    // 버퍼
    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void UpdateBuffer(BufferHandle handle, const void* data, size_t size) override;
    void BindBuffer(BufferHandle handle, uint32_t bind_point) override;

    // 텍스처
    TextureHandle CreateTexture(const TextureDesc& desc, const void* initial_data) override;
    void BindTexture(TextureHandle handle, uint32_t bind_point) override;

    // 샘플러
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void BindSampler(SamplerHandle handle, uint32_t bind_point) override;

    // 셰이더
    ShaderHandle CreateShader(const ShaderDesc& desc) override;
    void ReleaseShader(ShaderHandle handle) override;

    // 파이프라인
    PipelineHandle CreatePipeline(const PipelineDesc& desc) override;
    void BindPipeline(PipelineHandle handle) override;

    // 리소스 해제
    void ReleaseResource(uint64_t handle) override;

    // 프레임
    void BeginFrame() override;
    void EndFrame() override;

   private:
    // 내부 구조체
    struct VulkanSwapChain {
        vk::SwapchainKHR swapchain;
        std::vector<vk::Image> images;
        std::vector<vk::ImageView> image_views;
        vk::Extent2D extent;
        vk::RenderPass render_pass;
        std::vector<vk::Framebuffer> framebuffers;
    };
    struct VulkanBuffer {
        vk::Buffer buffer;
        size_t size_in_bytes = 0;
    };
    struct VulkanTexture {
        vk::Image image;
        vk::ImageView image_view;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    struct VulkanSampler {
        vk::Sampler sampler;
    };
    struct VulkanShader {
        vk::ShaderModule shader_module;
    };
    struct VulkanPipeline {
        vk::Pipeline pipeline;
        vk::PipelineLayout pipeline_layout;
    };

    // 리소스 보관
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

    // Win32 Window + Surface (앞으로의 과제)
    // (아직 Surface 생성 코드는 매우 단순표기)
    bool initialized_ = false;

    // 커맨드 풀 / 버퍼 / 동기화 등
    vk::CommandPool command_pool_;
    std::vector<vk::CommandBuffer> command_buffers_;

    // ...
    // 앞으로의 과제: 멀티스왑체인 이미지 동기화, MSAA, 큐브맵, 컴퓨트 등

   private:
    void InitVulkan();
    void CleanupVulkan();

    // swapchain
    SwapChainHandle CreateSwapChainInternal(const SwapChainDesc& desc);

    // 헬퍼
    vk::ShaderModule CreateShaderModule(const std::vector<char>& code);
    std::vector<char> ReadFile(const std::string& path);
};

}  // namespace Lumora
