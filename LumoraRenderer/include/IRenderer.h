#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Lumora {

// 리소스 핸들 정의
struct ResourceHandle {
    uint64_t id = 0;
    bool IsValid() const { return id != 0; }
};

struct SwapChainHandle : ResourceHandle {};
struct BufferHandle : ResourceHandle {};
struct TextureHandle : ResourceHandle {};
struct SamplerHandle : ResourceHandle {};
struct PipelineHandle : ResourceHandle {};
struct ShaderHandle : ResourceHandle {};

enum class Format {
    Unknown,      // Auto
    RGBA8_Unorm,  // vk::Format::eR8G8B8A8Unorm
    RGBA8_SRGB,
};

enum class MemoryUsage {
    Auto,      // VMA_MEMORY_USAGE_AUTO
    GpuOnly,   // VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    CpuToGpu,  // VMA_MEMORY_USAGE_AUTO_PREFER_HOST
};

// 스왑체인 생성 파라미터
struct SwapChainDesc {
    void* window_handle = nullptr;  // Win32: HWND
    int width = 1280;
    int height = 720;
    Format format;
    int bufferCount = 2;
    bool vsync = true;
};

enum class Usage { Vertex, Index, Uniform, Storage };

// 버퍼 생성 파라미터에 메모리 사용 정책(예: GPU_ONLY/CPU_TO_GPU 등)을 담을 수 있도록 확장
struct BufferDesc {
    Usage usage;
    uint8_t* data = nullptr;
    size_t size = 0;
    bool isDynamic = false;
    MemoryUsage memory_usage = MemoryUsage::Auto;
};

// 텍스처 생성 파라미터
struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    uint32_t array_layers = 1;
    Format format = Format::RGBA8_Unorm;

    bool usage_color_attachment = false;
    bool usage_storage_image = false;  // 새로 추가
};

enum class Filter {
    Nearest,
    Linear,
};

enum class AddressMode {
    Repeat,
    ClampToEdge,
};

// 샘플러 생성 파라미터
struct SamplerDesc {
    Filter filter_min = Filter::Linear;
    Filter filter_mag = Filter::Linear;

    AddressMode address_mode_u = AddressMode::Repeat;
    AddressMode address_mode_v = AddressMode::Repeat;
    AddressMode address_mode_w = AddressMode::Repeat;

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

enum class CullMode { None, Front, Back, FrontAndBack };

enum class FrontFace { CCW, CW };

// Shader Stage Flags
enum ShaderStageFlags {
    Vertex = 1 << 0,
    Fragment = 1 << 1,
    Compute = 1 << 2,
    // Add more stages as needed
};

enum class PolygonMode { Fill, Line, Point };

enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    // Add more blend factors as needed
};

enum class BlendOp {
    Add,
    Subtract,
    ReverseSubtract,
    // Add more blend operations as needed
};
enum class CompareOp { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };

// Vertex Layout Description
struct VertexLayoutDesc {
    struct AttributeDesc {
        uint32_t location;  // Shader location
        Format format;      // Attribute type
        uint32_t offset;    // Offset in the vertex structure
    };
    uint32_t stride;                        // Size of each vertex
    std::vector<AttributeDesc> attributes;  // List of attributes
};

// 파이프라인 생성 파라미터
struct PipelineDesc {
    ShaderHandle vertex_shader;
    ShaderHandle fragment_shader;
    // Depth, MSAA, Blend, etc. 확장 가능

    bool enable_depth_test = false;
    bool enable_depth_write = false;

    CullMode cull_mode = CullMode::Back;
    FrontFace front_face = FrontFace::CCW;
    PolygonMode polygon_mode = PolygonMode::Fill;

    bool blend_enable = false;
    // etc. (srcColorBlendFactor, dstColorBlendFactor, blendOp 등도 확장 가능)
};

struct ComputePipelineDesc {
    ShaderHandle compute_shader;
    // 필요 시, 스페셜라이제이션 상수, 워크그룹 크기 등 확장 가능
};

// 렌더러 인터페이스
class IRenderer {
   public:
    virtual ~IRenderer() = default;

    // 스왑체인
    virtual SwapChainHandle CreateSwapChain(const SwapChainDesc& desc) = 0;

    // 버퍼
    virtual BufferHandle CreateBuffer(const BufferDesc& desc, const void* initial_data = nullptr) = 0;
    virtual void UpdateBuffer(BufferHandle handle, const void* data, size_t size) = 0;
    virtual void BindBuffer(BufferHandle handle, uint32_t bind_point, uint32_t dynamicOffset = 0) = 0;

    // 텍스처
    virtual TextureHandle CreateTexture(const TextureDesc& desc, const void* initial_data = nullptr) = 0;
    virtual void BindTexture(TextureHandle handle, uint32_t bind_point, bool isStorage = false) = 0;

    // 샘플러
    virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
    virtual void BindSampler(SamplerHandle handle, uint32_t bind_point) = 0;

    // 셰이더
    virtual ShaderHandle CreateShader(const ShaderDesc& desc) = 0;

    // 파이프라인
    virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;
    virtual PipelineHandle CreatePipeline(const ComputePipelineDesc& desc) = 0;
    virtual void BindPipeline(PipelineHandle handle) = 0;

    // 리소스 해제
    virtual void ReleaseResource(SwapChainHandle handle) = 0;
    virtual void ReleaseResource(TextureHandle handle) = 0;
    virtual void ReleaseResource(SamplerHandle handle) = 0;
    virtual void ReleaseResource(PipelineHandle handle) = 0;
    virtual void ReleaseResource(ShaderHandle handle) = 0;

    virtual void DispatchCompute(uint32_t group_x, uint32_t group_y, uint32_t group_z) = 0;

    // renderer->Render([](IRenderer* r){
    //   r->BindPipeline(myPipeline);
    //   r->BindBuffer(myVbo);
    //   r->BindBuffer(myIbo);
    //   r->BindTexture(...);
    //   r->DrawIndexed(36); // e.g. a cube with 36 indices
    //});
    virtual void Render(std::function<void()> callback) = 0;

    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
                             int32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;

    // 파라미터: 기존 shaderHandle, 새 파일 경로 or Desc
    virtual bool ReloadShader(ShaderHandle handle, const ShaderDesc& new_desc) = 0;

    // 정적 생성 함수
    static std::unique_ptr<IRenderer> Create();
};

}  // namespace Lumora
