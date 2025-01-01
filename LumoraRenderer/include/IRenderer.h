#ifndef LUMORA_IRENDERER_H_
#define LUMORA_IRENDERER_H_

#include <cstdint>
#include <memory>
#include <string>

namespace Lumora
{

    // 핸들 정의
    using SwapChainHandle = uint64_t;
    using BufferHandle = uint64_t;
    using TextureHandle = uint64_t;
    using SamplerHandle = uint64_t;
    using PipelineHandle = uint64_t;
    using ShaderHandle = uint64_t;

    // 스왑체인 파라미터
    struct SwapChainDesc
    {
        // Win32라면 HWND가 될 수 있습니다.
        void *window_handle = nullptr;
        int width = 1280;
        int height = 720;
        bool vsync = true;
    };

    // 버퍼, 텍스처, 샘플러, 파이프라인 등 각종 Desc 구조체들
    struct BufferDesc
    {
        size_t size_in_bytes = 0;
        bool usage_uniform_buffer = false;
        bool usage_vertex_buffer = false;
        bool usage_index_buffer = false;
        bool usage_transfer_src = false;
        bool usage_transfer_dst = false;
    };

    struct TextureDesc
    {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct SamplerDesc
    {
        // 필터/주소모드 등
    };

    struct ShaderDesc
    {
        std::string file_path; // SPIR-V 등
    };

    struct PipelineDesc
    {
        ShaderHandle vertex_shader = 0;
        ShaderHandle fragment_shader = 0;
    };

    // 렌더러 인터페이스
    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        // 스왑체인
        virtual SwapChainHandle CreateSwapChain(const SwapChainDesc &desc) = 0;

        // 버퍼
        virtual BufferHandle CreateBuffer(const BufferDesc &desc) = 0;
        virtual void UpdateBuffer(BufferHandle handle, const void *data, size_t size) = 0;
        virtual void BindBuffer(BufferHandle handle, uint32_t bind_point) = 0;

        // 텍스처
        virtual TextureHandle CreateTexture(const TextureDesc &desc,
                                            const void *initial_data = nullptr) = 0;
        virtual void BindTexture(TextureHandle handle, uint32_t bind_point) = 0;

        // 샘플러
        virtual SamplerHandle CreateSampler(const SamplerDesc &desc) = 0;
        virtual void BindSampler(SamplerHandle handle, uint32_t bind_point) = 0;

        // 셰이더
        virtual ShaderHandle CreateShader(const ShaderDesc &desc) = 0;
        virtual void ReleaseShader(ShaderHandle handle) = 0;

        // 파이프라인
        virtual PipelineHandle CreatePipeline(const PipelineDesc &desc) = 0;
        virtual void BindPipeline(PipelineHandle handle) = 0;

        // 리소스 해제
        virtual void ReleaseResource(uint64_t handle) = 0;

        // 프레임
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        // 스태틱 함수 -> VulkanRenderer 생성
        static std::unique_ptr<IRenderer> Create();
    };

} // namespace Lumora

#endif // LUMORA_IRENDERER_H_
