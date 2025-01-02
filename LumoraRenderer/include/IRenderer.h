#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lumora {

// Resource handle definition
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
    kUnknown,
    kRGBA8,
    kBGRA8,
    kRGBA16F,
    kRGBA32F,
    kRGB8,
    kRGB16F,
    kRGB32F,
    kDepth24Stencil8,
    kDepth32F,
    kR8,
    kR16F,
    kR32F,
    kRG8,
    kRG16F,
    kRG32F,
    kSRGB8,
    kSRGBA8,
    kSRGBA8Unorm,
    kRGBA8Unorm,
    kBGRA8Unorm,
    kRGB8Unorm,
    kR8Unorm,
    kRG8Unorm,
    kRGBA16Unorm,
    kRGB16Unorm,
    kR16Unorm,
    kRG16Unorm
};

enum class MemoryUsage { kAuto, kGpuOnly, kCpuToGpu };

struct SwapChainDesc {
    void* window_handle = nullptr;
    int width = 1280;
    int height = 720;
    Format format;
    int buffer_count = 2;
    bool vsync = true;
};

enum class BufferUsage { kVertex, kIndex, kUniform, kStorage };

struct BufferDesc {
    BufferUsage usage;
    uint8_t* data = nullptr;  // initial data
    size_t size = 0;
    bool is_dynamic = false;
    MemoryUsage memory_usage = MemoryUsage::kAuto;
};

struct TextureDesc {
    uint8_t* data = nullptr;  // initial data
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    uint32_t array_layers = 1;
    Format format = Format::kSRGBA8Unorm;

    bool usage_color_attachment = false;
    bool usage_storage_image = false;
};

enum class Filter { kNearest, kLinear };

enum class AddressMode { kRepeat, kClampToEdge };

struct SamplerDesc {
    Filter filter_min = Filter::kLinear;
    Filter filter_mag = Filter::kLinear;
    AddressMode address_mode_u = AddressMode::kRepeat;
    AddressMode address_mode_v = AddressMode::kRepeat;
    AddressMode address_mode_w = AddressMode::kRepeat;
    float mip_lod_bias = 0.0f;
    float min_lod = 0.0f;
    float max_lod = 1000.0f;
    bool enable_anisotropy = false;
    float max_anisotropy = 1.0f;
};

struct ShaderDesc {
    std::string file_path;
};

enum class CullMode { kNone, kFront, kBack, kFrontAndBack };

enum class FrontFace { kCcw, kCw };

enum class PolygonMode { kFill, kLine, kPoint };

enum class BlendFactor {
    kZero,
    kOne,
    kSrcColor,
    kOneMinusSrcColor,
    kDstColor,
    kOneMinusDstColor,
    kSrcAlpha,
    kOneMinusSrcAlpha,
    kDstAlpha,
    kOneMinusDstAlpha,
    kConstantColor,
    kOneMinusConstantColor,
    kConstantAlpha,
    kOneMinusConstantAlpha,
    kSrcAlphaSaturate
};

enum class BlendOp { kAdd, kSubtract, kReverseSubtract, kMin, kMax };

enum class CompareOp { kNever, kLess, kEqual, kLessOrEqual, kGreater, kNotEqual, kGreaterOrEqual, kAlways };

struct VertexLayoutDesc {
    struct AttributeDesc {
        uint32_t location;
        Format format;
        uint32_t offset;
    };
    uint32_t stride;
    std::vector<AttributeDesc> attributes;
};

struct ViewportDesc {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float min_depth = 0.0f;
    float max_depth = 1.0f;
};

struct ScissorDesc {
    int32_t offset_x = 0;
    int32_t offset_y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct RasterizationState {
    bool depth_clamp_enable = false;
    bool rasterizer_discard_enable = false;
    PolygonMode polygon_mode = PolygonMode::kFill;
    CullMode cull_mode = CullMode::kBack;
    FrontFace front_face = FrontFace::kCw;
};

struct ColorBlendState {
    bool blend_enable = false;
    BlendFactor src_color_blend_factor = BlendFactor::kOne;
    BlendFactor dst_color_blend_factor = BlendFactor::kZero;
    BlendOp color_blend_op = BlendOp::kAdd;
    BlendFactor src_alpha_blend_factor = BlendFactor::kOne;
    BlendFactor dst_alpha_blend_factor = BlendFactor::kZero;
    BlendOp alpha_blend_op = BlendOp::kAdd;
};

struct DepthStencilState {
    bool depth_test_enable = true;
    bool depth_write_enable = true;
    CompareOp depth_compare_op = CompareOp::kLess;
    bool stencil_test_enable = false;
};

struct PipelineDesc {
    ShaderHandle vertex_shader;
    ShaderHandle fragment_shader;
    VertexLayoutDesc vertex_layout_desc;
    ViewportDesc viewport;
    ScissorDesc scissor;
    RasterizationState rasterization;
    std::vector<ColorBlendState> color_blends;
    DepthStencilState depth_stencil;
    int sample_count = 1;
    PolygonMode polygon_mode = PolygonMode::kFill;
};

struct ComputePipelineDesc {
    ShaderHandle compute_shader;
};

class IRenderer {
   public:
    virtual ~IRenderer() = default;

    virtual SwapChainHandle CreateSwapChain(const SwapChainDesc& desc) = 0;
    virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
    virtual void UpdateBuffer(BufferHandle handle, const void* data, size_t size) = 0;
    virtual void BindBuffer(BufferHandle handle, uint32_t bind_point, uint32_t dynamic_offset = 0) = 0;
    virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
    virtual void BindTexture(TextureHandle handle, uint32_t bind_point, bool is_storage = false) = 0;
    virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
    virtual void BindSampler(SamplerHandle handle, uint32_t bind_point) = 0;
    virtual ShaderHandle CreateShader(const ShaderDesc& desc) = 0;
    virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;
    virtual PipelineHandle CreatePipeline(const ComputePipelineDesc& desc) = 0;
    virtual void BindPipeline(PipelineHandle handle) = 0;
    virtual void ReleaseResource(SwapChainHandle handle) = 0;
    virtual void ReleaseResource(TextureHandle handle) = 0;
    virtual void ReleaseResource(SamplerHandle handle) = 0;
    virtual void ReleaseResource(PipelineHandle handle) = 0;
    virtual void ReleaseResource(ShaderHandle handle) = 0;
    virtual void DispatchCompute(uint32_t group_x, uint32_t group_y, uint32_t group_z) = 0;
    // renderer->Render({[](){
    //   r->BindPipeline(myPipeline);
    //   r->BindBuffer(myVbo);
    //   r->BindBuffer(myIbo);
    //   r->BindTexture(...);
    //   r->DrawIndexed(36); // e.g. a cube with 36 indices
    //}});
    virtual void Render(std::vector<std::function<void()>> callbacks) = 0;
    virtual void DrawIndexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0,
                             int32_t vertex_offset = 0, uint32_t first_instance = 0) = 0;
    virtual bool ReloadShader(ShaderHandle handle, const ShaderDesc& new_desc) = 0;

    static std::unique_ptr<IRenderer> Create();
};

}  // namespace lumora
