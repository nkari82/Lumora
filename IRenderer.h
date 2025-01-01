#ifndef LUMORA_IRENDERER_H_
#define LUMORA_IRENDERER_H_

#include <cstdint>
#include <memory>
#include <string>

namespace Lumora
{

    // 핸들 정의
    using BufferHandle = uint64_t;
    using TextureHandle = uint64_t;
    using SamplerHandle = uint64_t;
    using PipelineHandle = uint64_t;
    using ShaderHandle = uint64_t;

    // 버퍼 생성 파라미터
    struct BufferDesc
    {
        size_t size_in_bytes = 0;
        bool usage_uniform_buffer = false;
        bool usage_vertex_buffer = false;
        bool usage_index_buffer = false;
        bool usage_transfer_src = false;
        bool usage_transfer_dst = false;
    };

    // 텍스처 생성 파라미터
    struct TextureDesc
    {
        uint32_t width = 0;
        uint32_t height = 0;
        // 포맷, mipLevels, arrayLayers 등 필요 시 확장
    };

    // 샘플러 생성 파라미터
    struct SamplerDesc
    {
        // 필터, 어드레스 모드 등
    };

    // 셰이더 생성 파라미터
    struct ShaderDesc
    {
        std::string file_path; // SPIR-V (.spv) 바이너리 경로
                               // Vertex/Fragment/Compute 구분, entry point 등 확장 가능
    };

    // 파이프라인 생성 파라미터
    struct PipelineDesc
    {
        ShaderHandle vertex_shader = 0;
        ShaderHandle fragment_shader = 0;
        // 렌더 상태, 블렌딩, 깊이버퍼, 톱로지 등 확장 가능
    };

    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        // 버퍼
        virtual BufferHandle CreateBuffer(const BufferDesc &desc) = 0;
        virtual void UpdateBuffer(BufferHandle handle, const void *data,
                                  size_t size) = 0;
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

        // 리소스 해제 (버퍼/텍스처/샘플러/파이프라인/셰이더 등)
        virtual void ReleaseResource(uint64_t handle) = 0;

        // 프레임 제어
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        // 정적 생성 함수 -> VulkanRenderer 반환
        static std::unique_ptr<IRenderer> Create();
    };

} // namespace Lumora

#endif // LUMORA_IRENDERER_H_
