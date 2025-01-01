#ifndef LUMORA_IRENDERER_H_
#define LUMORA_IRENDERER_H_

#include <cstdint>
#include <memory>

namespace Lumora
{

    // 리소스 핸들 정의
    using BufferHandle = uint64_t;
    using TextureHandle = uint64_t;
    using SamplerHandle = uint64_t;
    using PipelineHandle = uint64_t;

    // 플랫폼 독립적인 버퍼 생성 파라미터
    struct BufferDesc
    {
        size_t size_in_bytes = 0;
        bool usage_uniform_buffer = false;
        bool usage_vertex_buffer = false;
        bool usage_index_buffer = false;
        bool usage_transfer_src = false;
        bool usage_transfer_dst = false;
    };

    // 플랫폼 독립적인 텍스처 생성 파라미터
    struct TextureDesc
    {
        uint32_t width = 0;
        uint32_t height = 0;
        // 포맷, 레이아웃, mipLevels 등 필요한 필드들은 계속 확장 가능
    };

    // 플랫폼 독립적인 샘플러 생성 파라미터
    struct SamplerDesc
    {
        // 필터링, 어드레스 모드 등 계속 확장 가능
    };

    // 플랫폼 독립적인 파이프라인 생성 파라미터
    struct PipelineDesc
    {
        // 셰이더, 레스터라이저/블렌딩/깊이버퍼 옵션 등 확장 가능
    };

    // 플랫폼 독립적인 렌더러 인터페이스
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

        // 파이프라인
        virtual PipelineHandle CreatePipeline(const PipelineDesc &desc) = 0;
        virtual void BindPipeline(PipelineHandle handle) = 0;

        // 리소스 해제
        virtual void ReleaseResource(uint64_t handle) = 0;

        // VulkanRenderer를 생성하는 스태틱 함수
        static std::unique_ptr<IRenderer> Create();
    };

} // namespace Lumora

#endif // LUMORA_IRENDERER_H_
