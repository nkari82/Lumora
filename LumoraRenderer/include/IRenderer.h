#ifndef LUMORA_IRENDERER_H_
#define LUMORA_IRENDERER_H_

#include <cstdint>
#include <memory>
#include <string>

// 이 헤더는 vulkan, vma 등 어떤 의존성도 드러내지 않음

namespace Lumora {

// 리소스 핸들
using SwapChainHandle = uint64_t;
using BufferHandle = uint64_t;
using TextureHandle = uint64_t;
using SamplerHandle = uint64_t;
using PipelineHandle = uint64_t;
using ShaderHandle = uint64_t;

// 스왑체인
struct SwapChainDesc {
    void* window_handle = nullptr;  // Win32: HWND
    int width = 1280;
    int height = 720;
    bool vsync = true;
};

// 버퍼
struct BufferDesc {
    size_t size_in_bytes = 0;
    bool usage_uniform_buffer = false;
    bool usage_vertex_buffer = false;
    bool usage_index_buffer = false;
    bool usage_transfer_src = false;
    bool usage_transfer_dst = false;
};

// 텍스처
struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    bool is_cube_map = false;  // 앞으로의 과제 (큐브맵)
    bool is_3d = false;        // 3D 텍스처
                               // mipLevels, arrayLayers, format, etc. 확장 가능
};

// 샘플러
struct SamplerDesc {
    // 필터링, 어드레스 모드, mipmap 모드 등
};

// 셰이더
struct ShaderDesc {
    std::string file_path;
    // shader stage, entry point, etc.
};

// 파이프라인 (그래픽스 / 컴퓨트 구분 가능)
struct PipelineDesc {
    ShaderHandle vertex_shader = 0;
    ShaderHandle fragment_shader = 0;
    // 앞으로의 과제: compute_shader, geometry_shader 등
};

// IRenderer
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

    // 스태틱 함수
    static std::unique_ptr<IRenderer> Create();
};

}  // namespace Lumora

#endif  // LUMORA_IRENDERER_H_
