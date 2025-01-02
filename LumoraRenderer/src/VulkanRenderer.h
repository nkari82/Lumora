#pragma once

namespace Lumora {

class VulkanRenderer : public IRenderer {
   public:
    VulkanRenderer();
    ~VulkanRenderer() override;

    // IRenderer 구현
    SwapChainHandle CreateSwapChain(const SwapChainDesc& desc) override;
    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void UpdateBuffer(BufferHandle handle, const void* data, size_t size) override;
    void BindBuffer(BufferHandle handle, uint32_t bind_point, uint32_t dynamicOffset) override;

    TextureHandle CreateTexture(const TextureDesc& desc, const void* initial_data) override;
    void BindTexture(TextureHandle handle, uint32_t bind_point, bool isStorage) override;

    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void BindSampler(SamplerHandle handle, uint32_t bind_point) override;

    ShaderHandle CreateShader(const ShaderDesc& desc) override;
    void ReleaseShader(ShaderHandle handle) override;

    PipelineHandle CreatePipeline(const PipelineDesc& desc) override;
    void BindPipeline(PipelineHandle handle) override;

    // Compute 파이프라인
    PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override;

    void ReleaseResource(uint64_t handle) override;

    void BeginFrame() override;
    void EndFrame() override;

    // PushConstants 구현
    void PushConstants(uint32_t offset, uint32_t size, const void* data) override;  // #FIXME 사라질 것

    void DispatchCompute(uint32_t group_x, uint32_t group_y, uint32_t group_z) override;

    void ResourceBarrier(uint64_t resource_handle, ResourceLayout old_layout, ResourceLayout new_layout) override;

    // 새로 추가: DescriptorSet 관련 함수
    uint64_t CreateDescriptorSet(const DescriptorSetDesc& desc) override;
    void BindDescriptorSet(uint64_t pipelineHandle, uint64_t descriptorSetHandle, uint32_t index) override;

    bool ReloadShader(ShaderHandle handle, const ShaderDesc& new_desc) override;

    void SetRenderCallback(RenderCallbackFn callback) override;

    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;

   private:
    // 내부 구조체
    struct VulkanSwapChain {
        vk::SwapchainKHR swapchain;
        std::vector<vk::Image> images;
        std::vector<vk::ImageView> image_views;
        vk::Extent2D extent;
        vk::RenderPass render_pass;
        std::vector<vk::Framebuffer> framebuffers;

        // depth
        vk::Image depth_image;
        vk::ImageView depth_view;
        VmaAllocation depth_alloc = nullptr;
    };

    struct VulkanBuffer {
        vk::Buffer buffer;
        VmaAllocation allocation = nullptr;
        size_t size_in_bytes = 0;

        // 새로 추가: MemoryUsage(추상) -> 실제 VMA allocationFlag
        // (실제로는 VulkanRenderer.cpp에서 desc.memory_usage에 따라 VmaAllocationCreateInfo 설정)
    };

    struct VulkanTexture {
        vk::Image image;
        vk::ImageView image_view;
        VmaAllocation allocation = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        // 새로 추가: mip_levels, array_layers, format
        // (마찬가지로 VulkanRenderer.cpp에서 desc 사용 시 처리)
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

        bool depth_test_enable = false;
        bool depth_write_enable = false;
        // etc.
    };

    struct BoundBufferInfo {
        BufferHandle handle = 0;
        uint32_t dynamicOffset = 0;
    };

    // DescriptorSet을 보관할 자료구조
    // 예: descriptorSetHandle -> vk::DescriptorSet 매핑
    std::unordered_map<uint64_t, vk::DescriptorSet> m_descriptorSets;
    uint64_t m_nextDescriptorSetHandle = 1;

    RenderCallbackFn render_callback_ = nullptr;
    // 리소스 배열
    std::vector<VulkanSwapChain> swapchains_;
    std::vector<VulkanBuffer> buffers_;
    std::vector<VulkanTexture> textures_;
    std::vector<VulkanSampler> samplers_;
    std::vector<VulkanPipeline> pipelines_;
    std::unordered_map<ShaderHandle, VulkanShader> shaders_;

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

    // VMA
    VmaAllocator allocator_ = nullptr;

    // Command Pool + 커맨드 버퍼 (간단화)
    vk::CommandPool command_pool_;
    std::vector<vk::CommandBuffer> command_buffers_;

    // 동기화 (한 프레임)
    vk::Semaphore image_available_;
    vk::Semaphore render_finished_;
    vk::Fence in_flight_fence_;

    // 추가: descriptor 관련 멤버 (필요 없다면 스킵)
    vk::DescriptorPool descriptor_pool_;
    vk::DescriptorSetLayout descriptor_set_layout_;
    std::vector<vk::DescriptorSet> descriptor_sets_;

    // begin perframe
    // e.g. kMaxFramesInFlight = 2 or 3
    static const int kMaxFramesInFlight = 2;

    // 스왑체인 이미지 개수 (실제 acquireNextImageKHR 후 반환되는 count)
    // 일정하다고 가정
    uint32_t m_swapchainImageCount = 0;
    uint32_t m_currentSwapchainImageIndex = 0;
    uint32_t m_currentFrame = 0;  // 현재 프레임 인덱스

    // CommandBuffer, Semaphores, Fences, DescriptorSets 등도 "per swapchain image" 또는 "per in-flight"로 구성
    std::vector<vk::CommandBuffer> m_commandBuffers;  // command_buffers_ size=swapchainImageCount
    std::vector<vk::Semaphore> m_imageAvailable;      // image_available_
    std::vector<vk::Semaphore> m_renderFinished;      // render_finished_
    std::vector<vk::Fence> m_inFlightFences;          // in_flight_fence_

    // DescriptorSets도 스왑체인 이미지 개수만큼
    std::vector<vk::DescriptorSet> m_descriptorSets;  // descriptor_sets_ size=swapchainImageCount
    // end perframe

    BufferHandle m_boundVertexBufferHandle_;
    BufferHandle m_boundIndexBufferHandle_;

    BufferHandle ubo_handle_ = 0;
    BufferHandle vbo_handle_ = 0;
    BufferHandle ibo_handle_ = 0;
    uint32_t index_count_ = 0;

    // Push Constants를 적용할 StageFlags 지정 (간단히 Vertex/Fragment로 가정)
    // 혹은 PipelineDesc에서 StageFlags를 받아올 수도 있음. #FIXME 사라질 것
    vk::ShaderStageFlags m_pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    bool enable_multiple_subpass_ = false;  // 예: 디버그 용도 / 파이프라인 생성 시 참고

    bool initialized_ = false;

   private:
    void InitVulkan();
    void CleanupVulkan();
    void InitVMA();

    SwapChainHandle CreateSwapChainInternal(const SwapChainDesc& desc);

    // 버퍼 업데이트 시 스테이징 복사 예시
    void UploadDataToBuffer(const void* data, size_t size, vk::Buffer dst_buffer);

    // test
    void CreateTestDescriptorSet(BufferHandle ubo, TextureHandle tex, SamplerHandle samp);
    vk::ShaderModule CreateShaderModule(const std::vector<char>& code);
    std::vector<char> ReadFile(const std::string& filename);

    // 테스트용 삼각형 예시
    void RecordCommandBuffer(vk::CommandBuffer cmd, uint32_t image_index);
};

}  // namespace Lumora
