#ifndef LUMORA_IRENDERER_H_
#define LUMORA_IRENDERER_H_

#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace Lumora
{

    // 리소스 핸들 정의
    using BufferHandle = uint64_t;
    using TextureHandle = uint64_t;
    using SamplerHandle = uint64_t;
    using PipelineHandle = uint64_t;
    using ShaderHandle = uint64_t; // 앞으로 셰이더만 별도 관리 가능하도록

    //
    // 플랫폼 독립적인 버퍼 생성 파라미터
    //
    struct BufferDesc
    {
        size_t size_in_bytes = 0;
        bool usage_uniform_buffer = false;
        bool usage_vertex_buffer = false;
        bool usage_index_buffer = false;
        bool usage_transfer_src = false;
        bool usage_transfer_dst = false;
    };

    //
    // 플랫폼 독립적인 텍스처 생성 파라미터
    //
    struct TextureDesc
    {
        uint32_t width = 0;
        uint32_t height = 0;
        // 포맷, mipLevels, 레이아웃, 큐브맵 등 확장 가능
    };

    //
    // 플랫폼 독립적인 샘플러 생성 파라미터
    //
    struct SamplerDesc
    {
        // 필터링, 어드레스 모드, mipmap 모드 등 확장 가능
    };

    //
    // 플랫폼 독립적인 셰이더 생성 파라미터
    //
    struct ShaderDesc
    {
        std::string file_path; // 스피르V 바이너리(.spv) 파일 경로
                               // 추가: 셰이더 단계(vertex/fragment/compute/etc), entry point, etc...
    };

    //
    // 플랫폼 독립적인 파이프라인 생성 파라미터
    //   - 앞으로 셰이더 핸들을 여기서 재활용할 수 있도록 확장
    //   - 기존에는 파일경로를 직접 받았지만, 별도 ShaderHandle로도 가능
    //
    struct PipelineDesc
    {
        ShaderHandle vertex_shader = 0;
        ShaderHandle fragment_shader = 0;
        // 렌더 상태, 블렌딩, 깊이버퍼, 톱로지, 레이아웃(DescriptorSetLayout) 등등 확장
    };

    //
    // 플랫폼 독립적인 렌더링 인터페이스
    //
    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        // ----- 버퍼 관련 -----
        virtual BufferHandle CreateBuffer(const BufferDesc &desc) = 0;
        virtual void UpdateBuffer(BufferHandle handle, const void *data,
                                  size_t size) = 0;
        virtual void BindBuffer(BufferHandle handle, uint32_t bind_point) = 0;

        // ----- 텍스처 관련 -----
        virtual TextureHandle CreateTexture(const TextureDesc &desc,
                                            const void *initial_data = nullptr) = 0;
        virtual void BindTexture(TextureHandle handle, uint32_t bind_point) = 0;

        // ----- 샘플러 관련 -----
        virtual SamplerHandle CreateSampler(const SamplerDesc &desc) = 0;
        virtual void BindSampler(SamplerHandle handle, uint32_t bind_point) = 0;

        // ----- 셰이더(단일) 관련 -----
        virtual ShaderHandle CreateShader(const ShaderDesc &desc) = 0;
        // 필요하면 UpdateShader, BindShader 등 추가. (보통은 파이프라인에서 셰이더를 사용)
        virtual void ReleaseShader(ShaderHandle handle) = 0;

        // ----- 파이프라인 관련 -----
        virtual PipelineHandle CreatePipeline(const PipelineDesc &desc) = 0;
        virtual void BindPipeline(PipelineHandle handle) = 0;

        // ----- 리소스 해제 -----
        virtual void ReleaseResource(uint64_t handle) = 0;

        // ----- 프레임 제어: Begin/End -----
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        // 스태틱 함수: VulkanRenderer 인스턴스화
        static std::unique_ptr<IRenderer> Create();
    };

} // namespace Lumora

#endif // LUMORA_IRENDERER_H_
