#ifndef LUMORA_IRENDERER_H_
#define LUMORA_IRENDERER_H_

#include <cstdint>
#include <memory>
#include <string>

namespace Lumora {

// 리소스 핸들 정의
using SwapChainHandle = uint64_t;
using BufferHandle = uint64_t;
using TextureHandle = uint64_t;
using SamplerHandle = uint64_t;
using PipelineHandle = uint64_t;
using ShaderHandle = uint64_t;

// 스왑체인 생성 파라미터
struct SwapChainDesc {
    void* window_handle = nullptr;  // Win32: HWND
    int width = 1280;
    int height = 720;
    bool vsync = true;
};

// 버퍼 생성 파라미터
struct BufferDesc {
    size_t size_in_bytes = 0;
    bool usage_uniform_buffer = false;
    bool usage_vertex_buffer = false;
    bool usage_index_buffer = false;
    bool usage_transfer_src = false;
    bool usage_transfer_dst = false;
    // VMA 힌트(예: GPU_ONLY, CPU_TO_GPU 등)를 여기에 추가해도 좋음
};

// 텍스처 생성 파라미터
struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    // 포맷, mipLevels, 레이아웃 등 필요시 확장
};

// 샘플러 생성 파라미터
struct SamplerDesc {
    // 필터링, 어드레스 모드 등
};

// 셰이더 생성 파라미터
struct ShaderDesc {
    std::string file_path;  // SPIR-V 바이너리
};

// 파이프라인 생성 파라미터
struct PipelineDesc {
    ShaderHandle vertex_shader = 0;
    ShaderHandle fragment_shader = 0;
    // Depth, MSAA, Blend, etc. 확장 가능
};

// 렌더러 인터페이스
class IRenderer {
   public:
    virtual ~IRenderer() = default;

    // 스왑체인
    virtual SwapChainHandle CreateSwapChain(const SwapChainDesc& desc) = 0;

    // 버퍼
    virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
    virtual void UpdateBuffer(BufferHandle handle, const void* data, size_t size) = 0;
    virtual void BindBuffer(BufferHandle handle, uint32_t bind_point) = 0;

    // 텍스처
    virtual TextureHandle CreateTexture(const TextureDesc& desc, const void* initial_data = nullptr) = 0;
    virtual void BindTexture(TextureHandle handle, uint32_t bind_point) = 0;

    // 샘플러
    virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
    virtual void BindSampler(SamplerHandle handle, uint32_t bind_point) = 0;

    // 셰이더
    virtual ShaderHandle CreateShader(const ShaderDesc& desc) = 0;
    virtual void ReleaseShader(ShaderHandle handle) = 0;

    // 파이프라인
    virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;
    virtual void BindPipeline(PipelineHandle handle) = 0;

    // 리소스 해제
    virtual void ReleaseResource(uint64_t handle) = 0;

    // 프레임
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    // 정적 생성 함수
    static std::unique_ptr<IRenderer> Create();
};

}  // namespace Lumora

#endif  // LUMORA_IRENDERER_H_
