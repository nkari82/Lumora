#pragma once

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

// 버퍼 생성 파라미터에 메모리 사용 정책(예: GPU_ONLY/CPU_TO_GPU 등)을 담을 수 있도록 확장
struct BufferDesc {
    size_t size_in_bytes = 0;
    bool usage_uniform_buffer = false;
    bool usage_vertex_buffer = false;
    bool usage_index_buffer = false;
    bool usage_transfer_src = false;
    bool usage_transfer_dst = false;
    // 새로 추가: VMA 할당 정책 등 Vulkan 세부 옵션
    enum class MemoryUsage {
        Auto,      // VMA_MEMORY_USAGE_AUTO
        GpuOnly,   // VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        CpuToGpu,  // VMA_MEMORY_USAGE_AUTO_PREFER_HOST
                   // ...
    } memory_usage = MemoryUsage::Auto;
};

// 텍스처 생성 파라미터
struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    uint32_t array_layers = 1;
    // Vulkan 포맷
    // (실제 enum이 아닌 추상 enum으로 해도 되지만 여기선 간단화)
    enum class Format {
        RGBA8_Unorm,  // vk::Format::eR8G8B8A8Unorm
        RGBA8_SRGB,
        // ...
    } format = Format::RGBA8_Unorm;
};

// 샘플러 생성 파라미터
struct SamplerDesc {
    enum class Filter {
        Nearest,
        Linear,
    } filter_min = Filter::Linear,
      filter_mag = Filter::Linear;

    // 주소 모드
    enum class AddressMode {
        Repeat,
        ClampToEdge,
        // ...
    } address_mode_u = AddressMode::Repeat,
      address_mode_v = AddressMode::Repeat, address_mode_w = AddressMode::Repeat;

    float mip_lod_bias = 0.0f;
    float min_lod = 0.0f;
    float max_lod = 1000.0f;
    bool enable_anisotropy = false;
    float max_anisotropy = 1.0f;
};

// 셰이더 생성 파라미터
struct ShaderDesc {
    std::string file_path;  // SPIR-V 바이너리
};

// 파이프라인 생성 파라미터
struct PipelineDesc {
    ShaderHandle vertex_shader;
    ShaderHandle fragment_shader;
    // Depth, MSAA, Blend, etc. 확장 가능

    bool enable_depth_test = false;
    bool enable_depth_write = false;

    enum class CullMode { None, Front, Back, FrontAndBack } cull_mode = CullMode::Back;

    enum class FrontFace { CCW, CW } front_face = FrontFace::CCW;

    enum class PolygonMode { Fill, Line, Point } polygon_mode = PolygonMode::Fill;

    bool blend_enable = false;
    // etc. (srcColorBlendFactor, dstColorBlendFactor, blendOp 등도 확장 가능)
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
    virtual void ReleaseResource(uint64_t handle) = 0;  // #FIXME template handle.

    // 프레임
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    virtual void PushConstants(uint32_t offset, uint32_t size, const void* data) = 0;  // #FIXME 사라질 것

    // 정적 생성 함수
    static std::unique_ptr<IRenderer> Create();
};

}  // namespace Lumora
