#pragma once

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#if defined(_WINDLL)
#if defined(LUMORA_EXPORTS)
#define LUMORA_API __declspec(dllexport)
#else
#define LUMORA_API __declspec(dllimport)
#endif
#else
#define LUMORA_API
#endif

#include "Format.h"

namespace lumora {

// Resource handle definition
struct ResourceHandle {
    uint64_t id = 0;

    bool operator==(const ResourceHandle& other) const { return id == other.id; }
};

struct SwapChainHandle : ResourceHandle {};
struct BufferHandle : ResourceHandle {};
struct TextureHandle : ResourceHandle {};
struct SamplerHandle : ResourceHandle {};
struct PipelineHandle : ResourceHandle {};
struct ShaderHandle : ResourceHandle {};
struct FrameBufferHandle : ResourceHandle {};

enum class MemoryUsage { kAuto, kGpuOnly, kCpuToGpu };

enum class TextureUsage : uint32_t {
    kNone = 0x0,
    kSampled = 0x1,          // 샘플링 가능한 텍스처
    kRenderTarget = 0x2,     // 렌더 타겟으로 사용 (Output Attachment)
    kDepthStencil = 0x4,     // Depth/Stencil 용도로 사용
    kStorage = 0x8,          // Storage Image로 사용
    kInputAttachment = 0x10  // Input Attachment로 사용 (렌더패스에서)
};

inline TextureUsage operator|(TextureUsage lhs, TextureUsage rhs) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline TextureUsage operator&(TextureUsage lhs, TextureUsage rhs) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

inline TextureUsage& operator|=(TextureUsage& lhs, TextureUsage rhs) {
    lhs = lhs | rhs;
    return lhs;
}

inline TextureUsage& operator&=(TextureUsage& lhs, TextureUsage rhs) {
    lhs = lhs & rhs;
    return lhs;
}

struct WindowHandle {
    void* display;   // 예: HWND, Display*, ANativeWindow*, NSView*, 등
    void* platform;  // 예: HINSTANCE, X11 Window, 추가 정보 등
};

struct SwapChainDesc {
    WindowHandle window_handle;
    uint32_t width = 1280;
    uint32_t height = 720;
    Format color_format = Format::kR8G8B8Srgb;
    Format depth_format = Format::kD32SfloatS8Uint;
    int32_t buffer_count = 2;
    bool vsync = true;
};

enum class BufferUsage {
    kNone = 0x0,        // 기본값: 아무 용도도 없음
    kVertex = 0x1,      // Vertex Buffer
    kIndex = 0x2,       // Index Buffer (32비트가 기본)
    kIndex16 = 0x40,    // 16비트 Index Buffer
    kIndex32 = kIndex,  // 32비트 Index Buffer (기본값)
    kUniform = 0x4,     // Uniform Buffer
    kStorage = 0x8,     // Storage Buffer
    kIndirect = 0x10,   // Indirect Draw/Dispatch Buffer
    kDynamic = 0x20     // 동적 버퍼
};

inline BufferUsage operator|(BufferUsage lhs, BufferUsage rhs) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline bool operator&(BufferUsage lhs, BufferUsage rhs) {
    return (static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs)) != 0;
}

struct BufferDesc {
    BufferUsage usage;
    uint8_t* data = nullptr;  // initial data
    size_t size = 0;          // initial data size
    MemoryUsage memory_usage = MemoryUsage::kAuto;
};

enum class TextureType {
    k2D,      // 기본값: 2D 텍스처
    k3D,      // 3D 텍스처
    kCubeMap  // 큐브 맵 텍스처 (array 6으로 사용)
};

struct TextureDesc {
    TextureType type = TextureType::k2D;  // 텍스처 타입 (기본: 2D)
    Format format = Format::kR8G8B8Srgb;
    TextureUsage usage = TextureUsage::kSampled;
    uint32_t width = 0;   // 텍스처의 너비
    uint32_t height = 0;  // 텍스처의 높이
    uint32_t depth = 1;   // 텍스처 깊이 (3D 텍스처 전용, 기본값: 1)
    uint32_t mip_levels = 1;
    uint32_t array_layers = 1;
    MemoryUsage memory_usage = MemoryUsage::kAuto;
    uint8_t* data = nullptr;  // initial data
    size_t size = 0;          // initial data size
};

enum class Filter { kNearest, kLinear };

enum class AddressMode { kRepeat, kClampToEdge };

struct SamplerDesc {
    Filter mag_filter = Filter::kLinear;
    Filter min_filter = Filter::kLinear;
    AddressMode address_mode_u = AddressMode::kRepeat;
    AddressMode address_mode_v = AddressMode::kRepeat;
    AddressMode address_mode_w = AddressMode::kRepeat;
    float mip_lod_bias = 0.0f;
    float min_lod = 0.0f;
    float max_lod = 1000.0f;
    bool enable_anisotropy = false;
    float max_anisotropy = 1.0f;
};

enum class ShaderStage : uint32_t { kVertex = 1 << 0, kFragment = 1 << 1, kCompute = 1 << 2 };

struct ShaderDesc {
    std::string file_path;
    ShaderStage stage;
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

struct InputAttribute {
    uint32_t location;
    Format format;
    uint32_t offset;
};

struct VertexLayoutDesc {
    uint32_t stride;
    std::vector<InputAttribute> attributes;
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

enum class SampleCount { k1, k2, k4, k6, k8, k16, k32, k64 };

struct PipelineDesc {
    ShaderHandle vertex_shader;
    ShaderHandle fragment_shader;
    VertexLayoutDesc vertex_layout_desc;
    ViewportDesc viewport;
    ScissorDesc scissor;
    RasterizationState rasterization;
    std::vector<ColorBlendState> color_blends;
    DepthStencilState depth_stencil;
    SampleCount sample_count = SampleCount::k1;
    PolygonMode polygon_mode = PolygonMode::kFill;
    uint32_t pass = 0;
};

struct ComputePipelineDesc {
    ShaderHandle compute_shader;
};

enum class AttachmentLoadOp {
    kLoad,      // 기존 내용을 유지
    kClear,     // 기존 내용을 지우고 초기화
    kDontCare,  // 내용 무시
};

enum class AttachmentStoreOp {
    kStore,     // 결과 저장
    kDontCare,  // 결과 무시
};

struct AttachmentOptions {
    AttachmentLoadOp load_op = AttachmentLoadOp::kClear;     // 기본값: 클리어
    AttachmentStoreOp store_op = AttachmentStoreOp::kStore;  // 기본값: 저장
};

struct SubpassDesc {
    std::vector<uint32_t> color_attachments;   // 컬러 첨부
    std::optional<uint32_t> depth_attachment;  // Depth 첨부 (optional)
    std::vector<uint32_t> input_attachments;   // Input 첨부
};

struct RenderPassConfig {
    std::vector<std::array<float, 4>> clear_colors;           // 각 컬러 타겟에 대한 클리어 색상
    bool clear_depth = true;                                  // 깊이 클리어 여부
    float clear_depth_value = 1.0f;                           // 깊이 클리어 값
    uint32_t clear_stencil_value = 0;                         // 스텐실 클리어 값
    std::vector<AttachmentOptions> color_attachment_options;  // 각 컬러 타겟의 옵션
    AttachmentOptions depth_attachment_options;               // 깊이 타겟의 옵션
    std::vector<SubpassDesc> subpasses;                       // 서브패스들
};

struct FrameBufferDesc {
    std::vector<TextureHandle> color_targets;
    TextureHandle depth_target;  // Depth 타겟 (optional)
    uint32_t width;
    uint32_t height;
    RenderPassConfig config;
};

// Renderer Interface
class LUMORA_API IRenderer {
   public:
    virtual ~IRenderer() = default;

    virtual void Open(const char* app_name, const SwapChainDesc& desc) = 0;
    virtual void Close() = 0;

    virtual SwapChainHandle CreateSwapChain(const SwapChainDesc& desc) = 0;
    virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
    virtual FrameBufferHandle CreateFrameBuffer(const FrameBufferDesc& desc) = 0;
    virtual FrameBufferHandle CreateFrameBuffer(const SwapChainHandle& handle) = 0;
    virtual void UpdateBuffer(const BufferHandle& handle, const void* data, size_t size) = 0;
    virtual void BindBuffer(const BufferHandle& handle, uint32_t bind_point, uint32_t dynamic_offset = 0) = 0;
    virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
    virtual void BindTexture(const TextureHandle& handle, uint32_t bind_point) = 0;
    virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
    virtual void BindSampler(const SamplerHandle& handle, uint32_t bind_point) = 0;
    virtual ShaderHandle CreateShader(const ShaderDesc& desc) = 0;
    virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;
    virtual PipelineHandle CreatePipeline(const ComputePipelineDesc& desc) = 0;
    virtual void BindPipeline(const PipelineHandle& handle, const uint8_t* constants, size_t size) = 0;

    virtual void DispatchCompute(uint32_t group_x, uint32_t group_y, uint32_t group_z) = 0;

    virtual void BeginPass(const FrameBufferHandle& handle = {}) = 0;
    virtual void EndPass() = 0;
    virtual void NextPass() = 0;
    virtual void Resize(uint32_t new_width, uint32_t new_height) = 0;
    virtual void Resize(const SwapChainHandle& handle, uint32_t new_width, uint32_t new_height) = 0;
    virtual void Render(std::function<void()> callback) = 0;
    virtual void Render(const SwapChainHandle& handle, std::function<void()> callback) = 0;
    virtual void DrawIndexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0,
                             int32_t vertex_offset = 0, uint32_t first_instance = 0) = 0;
    virtual bool ReloadShader(const ShaderHandle& handle, const ShaderDesc& new_desc) = 0;

    virtual void ReleaseResource(const SwapChainHandle& handle) = 0;
    virtual void ReleaseResource(const TextureHandle& handle) = 0;
    virtual void ReleaseResource(const SamplerHandle& handle) = 0;
    virtual void ReleaseResource(const PipelineHandle& handle) = 0;
    virtual void ReleaseResource(const ShaderHandle& handle) = 0;
    virtual void ReleaseResource(const FrameBufferHandle& handle) = 0;

    static std::unique_ptr<IRenderer> Create();
};

}  // namespace lumora

// example
// 단일 서브패스
// FrameBufferDesc pass_desc{};
// pass_desc.color_targets = {render_target};
// pass_desc.clear_colors = {{0.2f, 0.3f, 0.4f, 1.0f}};
// pass_desc.depth_target = depth_target;
// subpasses가 채워지지 않았을 경우 기본은 내부적으로 kWrite로 하나의 subpass가 만들어진다.
// pass_desc.subpasses = {{
//     {
//         .color_attachments = {{render_target, AttachmentAccess::kWrite}},
//         .depth_attachment = {depth_target, AttachmentAccess::kWrite},
//     },
// }};
//
// renderer->Render(swapchain, [&]() {
//     renderer->BindPass(framebuffer);
//     renderer->BindPipeline(my_pipeline);
//     renderer->DrawIndexed(36);
//     renderer->EndPass();
// });

// 멀티 서브패스
// FrameBufferDesc pass_desc{};
// pass_desc.color_targets = {render_target1, render_target2};
// pass_desc.clear_colors = {{0.2f, 0.3f, 0.4f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
// pass_desc.depth_target = depth_target;
//
// pass_desc.subpasses = {
//    {
//        .color_attachments = {{render_target1, AttachmentAccess::kWrite}},
//        .depth_attachment = {depth_target, AttachmentAccess::kWrite},
//    },
//    {
//        .color_attachments = {{render_target2, AttachmentAccess::kWrite}},
//        .input_attachments = {{render_target1, AttachmentAccess::kRead}},
//        .depth_attachment = {depth_target, AttachmentAccess::kRead},
//    },
// };
//
// renderer->Render([&]() {
//    renderer->BindPass();
//    renderer->BindPipeline(my_pipeline1);
//    renderer->DrawIndexed(36);
//    renderer->NextPass();
//    renderer->BindPipeline(my_pipeline2);
//    renderer->DrawIndexed(36);
//    renderer->EndPass();
// });
//
// renderer->Render(swapchain, {[](){
//   renderer->BeginPass(framebuffer); // BeginPass
//   // 렌더패스를 호출한다.
//   renderer->BindPipeline(my_pipeline);
//   renderer->BindBuffer(my_vbo);
//   renderer->BindBuffer(my_ibo);
//   renderer->BindTexture(...);
//   renderer->DrawIndexed(36); // e.g. a cube with 36 indices
//   renderer->EndPass();
// }});