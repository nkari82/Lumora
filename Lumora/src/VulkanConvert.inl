
inline vk::CompareOp VulkanRenderer::Convert(CompareOp op) {
    switch (op) {
        case CompareOp::kNever:
            return vk::CompareOp::eNever;
        case CompareOp::kLess:
            return vk::CompareOp::eLess;
        case CompareOp::kEqual:
            return vk::CompareOp::eEqual;
        case CompareOp::kLessOrEqual:
            return vk::CompareOp::eLessOrEqual;
        case CompareOp::kGreater:
            return vk::CompareOp::eGreater;
        case CompareOp::kNotEqual:
            return vk::CompareOp::eNotEqual;
        case CompareOp::kGreaterOrEqual:
            return vk::CompareOp::eGreaterOrEqual;
        case CompareOp::kAlways:
            return vk::CompareOp::eAlways;
        default:
            return vk::CompareOp::eNever;
    }
}

inline vk::BlendFactor VulkanRenderer::Convert(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::kZero:
            return vk::BlendFactor::eZero;
        case BlendFactor::kOne:
            return vk::BlendFactor::eOne;
        case BlendFactor::kSrcColor:
            return vk::BlendFactor::eSrcColor;
        case BlendFactor::kOneMinusSrcColor:
            return vk::BlendFactor::eOneMinusSrcColor;
        case BlendFactor::kDstColor:
            return vk::BlendFactor::eDstColor;
        case BlendFactor::kOneMinusDstColor:
            return vk::BlendFactor::eOneMinusDstColor;
        case BlendFactor::kSrcAlpha:
            return vk::BlendFactor::eSrcAlpha;
        case BlendFactor::kOneMinusSrcAlpha:
            return vk::BlendFactor::eOneMinusSrcAlpha;
        case BlendFactor::kDstAlpha:
            return vk::BlendFactor::eDstAlpha;
        case BlendFactor::kOneMinusDstAlpha:
            return vk::BlendFactor::eOneMinusDstAlpha;
        case BlendFactor::kConstantColor:
            return vk::BlendFactor::eConstantColor;
        case BlendFactor::kOneMinusConstantColor:
            return vk::BlendFactor::eOneMinusConstantColor;
        case BlendFactor::kConstantAlpha:
            return vk::BlendFactor::eConstantAlpha;
        case BlendFactor::kOneMinusConstantAlpha:
            return vk::BlendFactor::eOneMinusConstantAlpha;
        case BlendFactor::kSrcAlphaSaturate:
            return vk::BlendFactor::eSrcAlphaSaturate;
        default:
            return vk::BlendFactor::eZero;
    }
}

inline vk::BlendOp VulkanRenderer::Convert(BlendOp op) {
    switch (op) {
        case BlendOp::kAdd:
            return vk::BlendOp::eAdd;
        case BlendOp::kSubtract:
            return vk::BlendOp::eSubtract;
        case BlendOp::kReverseSubtract:
            return vk::BlendOp::eReverseSubtract;
        case BlendOp::kMin:
            return vk::BlendOp::eMin;
        case BlendOp::kMax:
            return vk::BlendOp::eMax;
        default:
            return vk::BlendOp::eAdd;
    }
}

inline Format VulkanRenderer::Convert(vk::Format format) {
    switch (format) {
        case vk::Format::eUndefined:
            return Format::kUndefined;
        case vk::Format::eR4G4UnormPack8:
            return Format::kR4G4UnormPack8;
        case vk::Format::eR4G4B4A4UnormPack16:
            return Format::kR4G4B4A4UnormPack16;
        case vk::Format::eB4G4R4A4UnormPack16:
            return Format::kB4G4R4A4UnormPack16;
        case vk::Format::eR5G6B5UnormPack16:
            return Format::kR5G6B5UnormPack16;
        case vk::Format::eB5G6R5UnormPack16:
            return Format::kB5G6R5UnormPack16;
        case vk::Format::eR5G5B5A1UnormPack16:
            return Format::kR5G5B5A1UnormPack16;
        case vk::Format::eB5G5R5A1UnormPack16:
            return Format::kB5G5R5A1UnormPack16;
        case vk::Format::eA1R5G5B5UnormPack16:
            return Format::kA1R5G5B5UnormPack16;
        case vk::Format::eR8Unorm:
            return Format::kR8Unorm;
        case vk::Format::eR8Snorm:
            return Format::kR8Snorm;
        case vk::Format::eR8Uscaled:
            return Format::kR8Uscaled;
        case vk::Format::eR8Sscaled:
            return Format::kR8Sscaled;
        case vk::Format::eR8Uint:
            return Format::kR8Uint;
        case vk::Format::eR8Sint:
            return Format::kR8Sint;
        case vk::Format::eR8Srgb:
            return Format::kR8Srgb;
        case vk::Format::eR8G8Unorm:
            return Format::kR8G8Unorm;
        case vk::Format::eR8G8Snorm:
            return Format::kR8G8Snorm;
        case vk::Format::eR8G8Uscaled:
            return Format::kR8G8Uscaled;
        case vk::Format::eR8G8Sscaled:
            return Format::kR8G8Sscaled;
        case vk::Format::eR8G8Uint:
            return Format::kR8G8Uint;
        case vk::Format::eR8G8Sint:
            return Format::kR8G8Sint;
        case vk::Format::eR8G8Srgb:
            return Format::kR8G8Srgb;
        case vk::Format::eR8G8B8Unorm:
            return Format::kR8G8B8Unorm;
        case vk::Format::eR8G8B8Snorm:
            return Format::kR8G8B8Snorm;
        case vk::Format::eR8G8B8Uscaled:
            return Format::kR8G8B8Uscaled;
        case vk::Format::eR8G8B8Sscaled:
            return Format::kR8G8B8Sscaled;
        case vk::Format::eR8G8B8Uint:
            return Format::kR8G8B8Uint;
        case vk::Format::eR8G8B8Sint:
            return Format::kR8G8B8Sint;
        case vk::Format::eR8G8B8Srgb:
            return Format::kR8G8B8Srgb;
        case vk::Format::eB8G8R8Unorm:
            return Format::kB8G8R8Unorm;
        case vk::Format::eB8G8R8Snorm:
            return Format::kB8G8R8Snorm;
        case vk::Format::eB8G8R8Uscaled:
            return Format::kB8G8R8Uscaled;
        case vk::Format::eB8G8R8Sscaled:
            return Format::kB8G8R8Sscaled;
        case vk::Format::eB8G8R8Uint:
            return Format::kB8G8R8Uint;
        case vk::Format::eB8G8R8Sint:
            return Format::kB8G8R8Sint;
        case vk::Format::eB8G8R8Srgb:
            return Format::kB8G8R8Srgb;
        case vk::Format::eR8G8B8A8Unorm:
            return Format::kR8G8B8A8Unorm;
        case vk::Format::eR8G8B8A8Snorm:
            return Format::kR8G8B8A8Snorm;
        case vk::Format::eR8G8B8A8Uscaled:
            return Format::kR8G8B8A8Uscaled;
        case vk::Format::eR8G8B8A8Sscaled:
            return Format::kR8G8B8A8Sscaled;
        case vk::Format::eR8G8B8A8Uint:
            return Format::kR8G8B8A8Uint;
        case vk::Format::eR8G8B8A8Sint:
            return Format::kR8G8B8A8Sint;
        case vk::Format::eR8G8B8A8Srgb:
            return Format::kR8G8B8A8Srgb;
        case vk::Format::eB8G8R8A8Unorm:
            return Format::kB8G8R8A8Unorm;
        case vk::Format::eB8G8R8A8Snorm:
            return Format::kB8G8R8A8Snorm;
        case vk::Format::eB8G8R8A8Uscaled:
            return Format::kB8G8R8A8Uscaled;
        case vk::Format::eB8G8R8A8Sscaled:
            return Format::kB8G8R8A8Sscaled;
        case vk::Format::eB8G8R8A8Uint:
            return Format::kB8G8R8A8Uint;
        case vk::Format::eB8G8R8A8Sint:
            return Format::kB8G8R8A8Sint;
        case vk::Format::eB8G8R8A8Srgb:
            return Format::kB8G8R8A8Srgb;
        case vk::Format::eA8B8G8R8UnormPack32:
            return Format::kA8B8G8R8UnormPack32;
        case vk::Format::eA8B8G8R8SnormPack32:
            return Format::kA8B8G8R8SnormPack32;
        case vk::Format::eA8B8G8R8UscaledPack32:
            return Format::kA8B8G8R8UscaledPack32;
        case vk::Format::eA8B8G8R8SscaledPack32:
            return Format::kA8B8G8R8SscaledPack32;
        case vk::Format::eA8B8G8R8UintPack32:
            return Format::kA8B8G8R8UintPack32;
        case vk::Format::eA8B8G8R8SintPack32:
            return Format::kA8B8G8R8SintPack32;
        case vk::Format::eA8B8G8R8SrgbPack32:
            return Format::kA8B8G8R8SrgbPack32;
        case vk::Format::eR16Unorm:
            return Format::kR16Unorm;
        case vk::Format::eR16Snorm:
            return Format::kR16Snorm;
        case vk::Format::eR16Uscaled:
            return Format::kR16Uscaled;
        case vk::Format::eR16Sscaled:
            return Format::kR16Sscaled;
        case vk::Format::eR16Uint:
            return Format::kR16Uint;
        case vk::Format::eR16Sint:
            return Format::kR16Sint;
        case vk::Format::eR16Sfloat:
            return Format::kR16Sfloat;
        case vk::Format::eR16G16Unorm:
            return Format::kR16G16Unorm;
        case vk::Format::eR16G16Snorm:
            return Format::kR16G16Snorm;
        case vk::Format::eR16G16Uscaled:
            return Format::kR16G16Uscaled;
        case vk::Format::eR16G16Sscaled:
            return Format::kR16G16Sscaled;
        case vk::Format::eR16G16Uint:
            return Format::kR16G16Uint;
        case vk::Format::eR16G16Sint:
            return Format::kR16G16Sint;
        case vk::Format::eR16G16Sfloat:
            return Format::kR16G16Sfloat;
        case vk::Format::eR16G16B16Unorm:
            return Format::kR16G16B16Unorm;
        case vk::Format::eR16G16B16Snorm:
            return Format::kR16G16B16Snorm;
        case vk::Format::eR16G16B16Uscaled:
            return Format::kR16G16B16Uscaled;
        case vk::Format::eR16G16B16Sscaled:
            return Format::kR16G16B16Sscaled;
        case vk::Format::eR16G16B16Uint:
            return Format::kR16G16B16Uint;
        case vk::Format::eR16G16B16Sint:
            return Format::kR16G16B16Sint;
        case vk::Format::eR16G16B16Sfloat:
            return Format::kR16G16B16Sfloat;
        case vk::Format::eR16G16B16A16Unorm:
            return Format::kR16G16B16A16Unorm;
        case vk::Format::eR16G16B16A16Snorm:
            return Format::kR16G16B16A16Snorm;
        case vk::Format::eR16G16B16A16Uscaled:
            return Format::kR16G16B16A16Uscaled;
        case vk::Format::eR16G16B16A16Sscaled:
            return Format::kR16G16B16A16Sscaled;
        case vk::Format::eR16G16B16A16Uint:
            return Format::kR16G16B16A16Uint;
        case vk::Format::eR16G16B16A16Sint:
            return Format::kR16G16B16A16Sint;
        case vk::Format::eR16G16B16A16Sfloat:
            return Format::kR16G16B16A16Sfloat;
        case vk::Format::eR32Uint:
            return Format::kR32Uint;
        case vk::Format::eR32Sint:
            return Format::kR32Sint;
        case vk::Format::eR32Sfloat:
            return Format::kR32Sfloat;
        case vk::Format::eR32G32Uint:
            return Format::kR32G32Uint;
        case vk::Format::eR32G32Sint:
            return Format::kR32G32Sint;
        case vk::Format::eR32G32Sfloat:
            return Format::kR32G32Sfloat;
        case vk::Format::eR32G32B32Uint:
            return Format::kR32G32B32Uint;
        case vk::Format::eR32G32B32Sint:
            return Format::kR32G32B32Sint;
        case vk::Format::eR32G32B32Sfloat:
            return Format::kR32G32B32Sfloat;
        case vk::Format::eR32G32B32A32Uint:
            return Format::kR32G32B32A32Uint;
        case vk::Format::eR32G32B32A32Sint:
            return Format::kR32G32B32A32Sint;
        case vk::Format::eR32G32B32A32Sfloat:
            return Format::kR32G32B32A32Sfloat;
        case vk::Format::eR64Uint:
            return Format::kR64Uint;
        case vk::Format::eR64Sint:
            return Format::kR64Sint;
        case vk::Format::eR64Sfloat:
            return Format::kR64Sfloat;
        case vk::Format::eR64G64Uint:
            return Format::kR64G64Uint;
        case vk::Format::eR64G64Sint:
            return Format::kR64G64Sint;
        case vk::Format::eR64G64Sfloat:
            return Format::kR64G64Sfloat;
        case vk::Format::eR64G64B64Uint:
            return Format::kR64G64B64Uint;
        case vk::Format::eR64G64B64Sint:
            return Format::kR64G64B64Sint;
        case vk::Format::eR64G64B64Sfloat:
            return Format::kR64G64B64Sfloat;
        case vk::Format::eR64G64B64A64Uint:
            return Format::kR64G64B64A64Uint;
        case vk::Format::eR64G64B64A64Sint:
            return Format::kR64G64B64A64Sint;
        case vk::Format::eR64G64B64A64Sfloat:
            return Format::kR64G64B64A64Sfloat;
        case vk::Format::eB10G11R11UfloatPack32:
            return Format::kB10G11R11UfloatPack32;
        case vk::Format::eE5B9G9R9UfloatPack32:
            return Format::kE5B9G9R9UfloatPack32;
        case vk::Format::eD16Unorm:
            return Format::kD16Unorm;
        case vk::Format::eX8D24UnormPack32:
            return Format::kX8D24UnormPack32;
        case vk::Format::eD32Sfloat:
            return Format::kD32Sfloat;
        case vk::Format::eS8Uint:
            return Format::kS8Uint;
        case vk::Format::eD16UnormS8Uint:
            return Format::kD16UnormS8Uint;
        case vk::Format::eD24UnormS8Uint:
            return Format::kD24UnormS8Uint;
        case vk::Format::eD32SfloatS8Uint:
            return Format::kD32SfloatS8Uint;
        case vk::Format::eBc1RgbUnormBlock:
            return Format::kBc1RgbUnormBlock;
        case vk::Format::eBc1RgbSrgbBlock:
            return Format::kBc1RgbSrgbBlock;
        case vk::Format::eBc1RgbaUnormBlock:
            return Format::kBc1RgbaUnormBlock;
        case vk::Format::eBc1RgbaSrgbBlock:
            return Format::kBc1RgbaSrgbBlock;
        case vk::Format::eBc2UnormBlock:
            return Format::kBc2UnormBlock;
        case vk::Format::eBc2SrgbBlock:
            return Format::kBc2SrgbBlock;
        case vk::Format::eBc3UnormBlock:
            return Format::kBc3UnormBlock;
        case vk::Format::eBc3SrgbBlock:
            return Format::kBc3SrgbBlock;
        case vk::Format::eBc4UnormBlock:
            return Format::kBc4UnormBlock;
        case vk::Format::eBc4SnormBlock:
            return Format::kBc4SnormBlock;
        case vk::Format::eBc5UnormBlock:
            return Format::kBc5UnormBlock;
        case vk::Format::eBc5SnormBlock:
            return Format::kBc5SnormBlock;
        case vk::Format::eBc6HUfloatBlock:
            return Format::kBc6HUfloatBlock;
        case vk::Format::eBc6HSfloatBlock:
            return Format::kBc6HSfloatBlock;
        case vk::Format::eBc7UnormBlock:
            return Format::kBc7UnormBlock;
        case vk::Format::eBc7SrgbBlock:
            return Format::kBc7SrgbBlock;
        case vk::Format::eEtc2R8G8B8UnormBlock:
            return Format::kEtc2R8G8B8UnormBlock;
        case vk::Format::eEtc2R8G8B8SrgbBlock:
            return Format::kEtc2R8G8B8SrgbBlock;
        case vk::Format::eEtc2R8G8B8A1UnormBlock:
            return Format::kEtc2R8G8B8A1UnormBlock;
        case vk::Format::eEtc2R8G8B8A1SrgbBlock:
            return Format::kEtc2R8G8B8A1SrgbBlock;
        case vk::Format::eEtc2R8G8B8A8UnormBlock:
            return Format::kEtc2R8G8B8A8UnormBlock;
        case vk::Format::eEtc2R8G8B8A8SrgbBlock:
            return Format::kEtc2R8G8B8A8SrgbBlock;
        case vk::Format::eEacR11UnormBlock:
            return Format::kEacR11UnormBlock;
        case vk::Format::eEacR11SnormBlock:
            return Format::kEacR11SnormBlock;
        case vk::Format::eEacR11G11UnormBlock:
            return Format::kEacR11G11UnormBlock;
        case vk::Format::eEacR11G11SnormBlock:
            return Format::kEacR11G11SnormBlock;
        case vk::Format::eAstc4x4UnormBlock:
            return Format::kAstc4x4UnormBlock;
        case vk::Format::eAstc4x4SrgbBlock:
            return Format::kAstc4x4SrgbBlock;
        case vk::Format::eAstc5x4UnormBlock:
            return Format::kAstc5x4UnormBlock;
        case vk::Format::eAstc5x4SrgbBlock:
            return Format::kAstc5x4SrgbBlock;
        case vk::Format::eAstc5x5UnormBlock:
            return Format::kAstc5x5UnormBlock;
        case vk::Format::eAstc5x5SrgbBlock:
            return Format::kAstc5x5SrgbBlock;
        case vk::Format::eAstc6x5UnormBlock:
            return Format::kAstc6x5UnormBlock;
        case vk::Format::eAstc6x5SrgbBlock:
            return Format::kAstc6x5SrgbBlock;
        case vk::Format::eAstc6x6UnormBlock:
            return Format::kAstc6x6UnormBlock;
        case vk::Format::eAstc6x6SrgbBlock:
            return Format::kAstc6x6SrgbBlock;
        case vk::Format::eAstc8x5UnormBlock:
            return Format::kAstc8x5UnormBlock;
        case vk::Format::eAstc8x5SrgbBlock:
            return Format::kAstc8x5SrgbBlock;
        case vk::Format::eAstc8x6UnormBlock:
            return Format::kAstc8x6UnormBlock;
        case vk::Format::eAstc8x6SrgbBlock:
            return Format::kAstc8x6SrgbBlock;
        case vk::Format::eAstc8x8UnormBlock:
            return Format::kAstc8x8UnormBlock;
        case vk::Format::eAstc8x8SrgbBlock:
            return Format::kAstc8x8SrgbBlock;
        case vk::Format::eAstc10x5UnormBlock:
            return Format::kAstc10x5UnormBlock;
        case vk::Format::eAstc10x5SrgbBlock:
            return Format::kAstc10x5SrgbBlock;
        case vk::Format::eAstc10x6UnormBlock:
            return Format::kAstc10x6UnormBlock;
        case vk::Format::eAstc10x6SrgbBlock:
            return Format::kAstc10x6SrgbBlock;
        case vk::Format::eAstc10x8UnormBlock:
            return Format::kAstc10x8UnormBlock;
        case vk::Format::eAstc10x8SrgbBlock:
            return Format::kAstc10x8SrgbBlock;
        case vk::Format::eAstc10x10UnormBlock:
            return Format::kAstc10x10UnormBlock;
        case vk::Format::eAstc10x10SrgbBlock:
            return Format::kAstc10x10SrgbBlock;
        case vk::Format::eAstc12x10UnormBlock:
            return Format::kAstc12x10UnormBlock;
        case vk::Format::eAstc12x10SrgbBlock:
            return Format::kAstc12x10SrgbBlock;
        case vk::Format::eAstc12x12UnormBlock:
            return Format::kAstc12x12UnormBlock;
        case vk::Format::eAstc12x12SrgbBlock:
            return Format::kAstc12x12SrgbBlock;
        case vk::Format::ePvrtc12BppUnormBlockIMG:
            return Format::kPvrtc12BppUnormBlockIMG;
        case vk::Format::ePvrtc14BppUnormBlockIMG:
            return Format::kPvrtc14BppUnormBlockIMG;
        case vk::Format::ePvrtc22BppUnormBlockIMG:
            return Format::kPvrtc22BppUnormBlockIMG;
        case vk::Format::ePvrtc24BppUnormBlockIMG:
            return Format::kPvrtc24BppUnormBlockIMG;
        case vk::Format::ePvrtc12BppSrgbBlockIMG:
            return Format::kPvrtc12BppSrgbBlockIMG;
        case vk::Format::ePvrtc14BppSrgbBlockIMG:
            return Format::kPvrtc14BppSrgbBlockIMG;
        case vk::Format::ePvrtc22BppSrgbBlockIMG:
            return Format::kPvrtc22BppSrgbBlockIMG;
        case vk::Format::ePvrtc24BppSrgbBlockIMG:
            return Format::kPvrtc24BppSrgbBlockIMG;
        case vk::Format::eR16G16Sfixed5NV:
            return Format::kR16G16Sfixed5NV;
        case vk::Format::eA1B5G5R5UnormPack16KHR:
            return Format::kA1B5G5R5UnormPack16KHR;
        case vk::Format::eA8UnormKHR:
            return Format::kA8UnormKHR;
        default:
            throw std::invalid_argument("Unsupported vk::Format");
    }
}

inline vk::Format VulkanRenderer::Convert(Format format) {
    switch (format) {
        case Format::kUndefined:
            return vk::Format::eUndefined;
        case Format::kR4G4UnormPack8:
            return vk::Format::eR4G4UnormPack8;
        case Format::kR4G4B4A4UnormPack16:
            return vk::Format::eR4G4B4A4UnormPack16;
        case Format::kB4G4R4A4UnormPack16:
            return vk::Format::eB4G4R4A4UnormPack16;
        case Format::kR5G6B5UnormPack16:
            return vk::Format::eR5G6B5UnormPack16;
        case Format::kB5G6R5UnormPack16:
            return vk::Format::eB5G6R5UnormPack16;
        case Format::kR5G5B5A1UnormPack16:
            return vk::Format::eR5G5B5A1UnormPack16;
        case Format::kB5G5R5A1UnormPack16:
            return vk::Format::eB5G5R5A1UnormPack16;
        case Format::kA1R5G5B5UnormPack16:
            return vk::Format::eA1R5G5B5UnormPack16;
        case Format::kR8Unorm:
            return vk::Format::eR8Unorm;
        case Format::kR8Snorm:
            return vk::Format::eR8Snorm;
        case Format::kR8Uscaled:
            return vk::Format::eR8Uscaled;
        case Format::kR8Sscaled:
            return vk::Format::eR8Sscaled;
        case Format::kR8Uint:
            return vk::Format::eR8Uint;
        case Format::kR8Sint:
            return vk::Format::eR8Sint;
        case Format::kR8Srgb:
            return vk::Format::eR8Srgb;
        case Format::kR8G8Unorm:
            return vk::Format::eR8G8Unorm;
        case Format::kR8G8Snorm:
            return vk::Format::eR8G8Snorm;
        case Format::kR8G8Uscaled:
            return vk::Format::eR8G8Uscaled;
        case Format::kR8G8Sscaled:
            return vk::Format::eR8G8Sscaled;
        case Format::kR8G8Uint:
            return vk::Format::eR8G8Uint;
        case Format::kR8G8Sint:
            return vk::Format::eR8G8Sint;
        case Format::kR8G8Srgb:
            return vk::Format::eR8G8Srgb;
        case Format::kR8G8B8Unorm:
            return vk::Format::eR8G8B8Unorm;
        case Format::kR8G8B8Snorm:
            return vk::Format::eR8G8B8Snorm;
        case Format::kR8G8B8Uscaled:
            return vk::Format::eR8G8B8Uscaled;
        case Format::kR8G8B8Sscaled:
            return vk::Format::eR8G8B8Sscaled;
        case Format::kR8G8B8Uint:
            return vk::Format::eR8G8B8Uint;
        case Format::kR8G8B8Sint:
            return vk::Format::eR8G8B8Sint;
        case Format::kR8G8B8Srgb:
            return vk::Format::eR8G8B8Srgb;
        case Format::kB8G8R8Unorm:
            return vk::Format::eB8G8R8Unorm;
        case Format::kB8G8R8Snorm:
            return vk::Format::eB8G8R8Snorm;
        case Format::kB8G8R8Uscaled:
            return vk::Format::eB8G8R8Uscaled;
        case Format::kB8G8R8Sscaled:
            return vk::Format::eB8G8R8Sscaled;
        case Format::kB8G8R8Uint:
            return vk::Format::eB8G8R8Uint;
        case Format::kB8G8R8Sint:
            return vk::Format::eB8G8R8Sint;
        case Format::kB8G8R8Srgb:
            return vk::Format::eB8G8R8Srgb;
        case Format::kR8G8B8A8Unorm:
            return vk::Format::eR8G8B8A8Unorm;
        case Format::kR8G8B8A8Snorm:
            return vk::Format::eR8G8B8A8Snorm;
        case Format::kR8G8B8A8Uscaled:
            return vk::Format::eR8G8B8A8Uscaled;
        case Format::kR8G8B8A8Sscaled:
            return vk::Format::eR8G8B8A8Sscaled;
        case Format::kR8G8B8A8Uint:
            return vk::Format::eR8G8B8A8Uint;
        case Format::kR8G8B8A8Sint:
            return vk::Format::eR8G8B8A8Sint;
        case Format::kR8G8B8A8Srgb:
            return vk::Format::eR8G8B8A8Srgb;
        case Format::kB8G8R8A8Unorm:
            return vk::Format::eB8G8R8A8Unorm;
        case Format::kB8G8R8A8Snorm:
            return vk::Format::eB8G8R8A8Snorm;
        case Format::kB8G8R8A8Uscaled:
            return vk::Format::eB8G8R8A8Uscaled;
        case Format::kB8G8R8A8Sscaled:
            return vk::Format::eB8G8R8A8Sscaled;
        case Format::kB8G8R8A8Uint:
            return vk::Format::eB8G8R8A8Uint;
        case Format::kB8G8R8A8Sint:
            return vk::Format::eB8G8R8A8Sint;
        case Format::kB8G8R8A8Srgb:
            return vk::Format::eB8G8R8A8Srgb;
        case Format::kA8B8G8R8UnormPack32:
            return vk::Format::eA8B8G8R8UnormPack32;
        case Format::kA8B8G8R8SnormPack32:
            return vk::Format::eA8B8G8R8SnormPack32;
        case Format::kA8B8G8R8UscaledPack32:
            return vk::Format::eA8B8G8R8UscaledPack32;
        case Format::kA8B8G8R8SscaledPack32:
            return vk::Format::eA8B8G8R8SscaledPack32;
        case Format::kA8B8G8R8UintPack32:
            return vk::Format::eA8B8G8R8UintPack32;
        case Format::kA8B8G8R8SintPack32:
            return vk::Format::eA8B8G8R8SintPack32;
        case Format::kA8B8G8R8SrgbPack32:
            return vk::Format::eA8B8G8R8SrgbPack32;
        case Format::kR16Unorm:
            return vk::Format::eR16Unorm;
        case Format::kR16Snorm:
            return vk::Format::eR16Snorm;
        case Format::kR16Uscaled:
            return vk::Format::eR16Uscaled;
        case Format::kR16Sscaled:
            return vk::Format::eR16Sscaled;
        case Format::kR16Uint:
            return vk::Format::eR16Uint;
        case Format::kR16Sint:
            return vk::Format::eR16Sint;
        case Format::kR16Sfloat:
            return vk::Format::eR16Sfloat;
        case Format::kR16G16Unorm:
            return vk::Format::eR16G16Unorm;
        case Format::kR16G16Snorm:
            return vk::Format::eR16G16Snorm;
        case Format::kR16G16Uscaled:
            return vk::Format::eR16G16Uscaled;
        case Format::kR16G16Sscaled:
            return vk::Format::eR16G16Sscaled;
        case Format::kR16G16Uint:
            return vk::Format::eR16G16Uint;
        case Format::kR16G16Sint:
            return vk::Format::eR16G16Sint;
        case Format::kR16G16Sfloat:
            return vk::Format::eR16G16Sfloat;
        case Format::kR16G16B16Unorm:
            return vk::Format::eR16G16B16Unorm;
        case Format::kR16G16B16Snorm:
            return vk::Format::eR16G16B16Snorm;
        case Format::kR16G16B16Uscaled:
            return vk::Format::eR16G16B16Uscaled;
        case Format::kR16G16B16Sscaled:
            return vk::Format::eR16G16B16Sscaled;
        case Format::kR16G16B16Uint:
            return vk::Format::eR16G16B16Uint;
        case Format::kR16G16B16Sint:
            return vk::Format::eR16G16B16Sint;
        case Format::kR16G16B16Sfloat:
            return vk::Format::eR16G16B16Sfloat;
        case Format::kR16G16B16A16Unorm:
            return vk::Format::eR16G16B16A16Unorm;
        case Format::kR16G16B16A16Snorm:
            return vk::Format::eR16G16B16A16Snorm;
        case Format::kR16G16B16A16Uscaled:
            return vk::Format::eR16G16B16A16Uscaled;
        case Format::kR16G16B16A16Sscaled:
            return vk::Format::eR16G16B16A16Sscaled;
        case Format::kR16G16B16A16Uint:
            return vk::Format::eR16G16B16A16Uint;
        case Format::kR16G16B16A16Sint:
            return vk::Format::eR16G16B16A16Sint;
        case Format::kR16G16B16A16Sfloat:
            return vk::Format::eR16G16B16A16Sfloat;
        case Format::kR32Uint:
            return vk::Format::eR32Uint;
        case Format::kR32Sint:
            return vk::Format::eR32Sint;
        case Format::kR32Sfloat:
            return vk::Format::eR32Sfloat;
        case Format::kR32G32Uint:
            return vk::Format::eR32G32Uint;
        case Format::kR32G32Sint:
            return vk::Format::eR32G32Sint;
        case Format::kR32G32Sfloat:
            return vk::Format::eR32G32Sfloat;
        case Format::kR32G32B32Uint:
            return vk::Format::eR32G32B32Uint;
        case Format::kR32G32B32Sint:
            return vk::Format::eR32G32B32Sint;
        case Format::kR32G32B32Sfloat:
            return vk::Format::eR32G32B32Sfloat;
        case Format::kR32G32B32A32Uint:
            return vk::Format::eR32G32B32A32Uint;
        case Format::kR32G32B32A32Sint:
            return vk::Format::eR32G32B32A32Sint;
        case Format::kR32G32B32A32Sfloat:
            return vk::Format::eR32G32B32A32Sfloat;
        case Format::kR64Uint:
            return vk::Format::eR64Uint;
        case Format::kR64Sint:
            return vk::Format::eR64Sint;
        case Format::kR64Sfloat:
            return vk::Format::eR64Sfloat;
        case Format::kR64G64Uint:
            return vk::Format::eR64G64Uint;
        case Format::kR64G64Sint:
            return vk::Format::eR64G64Sint;
        case Format::kR64G64Sfloat:
            return vk::Format::eR64G64Sfloat;
        case Format::kR64G64B64Uint:
            return vk::Format::eR64G64B64Uint;
        case Format::kR64G64B64Sint:
            return vk::Format::eR64G64B64Sint;
        case Format::kR64G64B64Sfloat:
            return vk::Format::eR64G64B64Sfloat;
        case Format::kR64G64B64A64Uint:
            return vk::Format::eR64G64B64A64Uint;
        case Format::kR64G64B64A64Sint:
            return vk::Format::eR64G64B64A64Sint;
        case Format::kR64G64B64A64Sfloat:
            return vk::Format::eR64G64B64A64Sfloat;
        case Format::kB10G11R11UfloatPack32:
            return vk::Format::eB10G11R11UfloatPack32;
        case Format::kE5B9G9R9UfloatPack32:
            return vk::Format::eE5B9G9R9UfloatPack32;
        case Format::kD16Unorm:
            return vk::Format::eD16Unorm;
        case Format::kX8D24UnormPack32:
            return vk::Format::eX8D24UnormPack32;
        case Format::kD32Sfloat:
            return vk::Format::eD32Sfloat;
        case Format::kS8Uint:
            return vk::Format::eS8Uint;
        case Format::kD16UnormS8Uint:
            return vk::Format::eD16UnormS8Uint;
        case Format::kD24UnormS8Uint:
            return vk::Format::eD24UnormS8Uint;
        case Format::kD32SfloatS8Uint:
            return vk::Format::eD32SfloatS8Uint;
        case Format::kBc1RgbUnormBlock:
            return vk::Format::eBc1RgbUnormBlock;
        case Format::kBc1RgbSrgbBlock:
            return vk::Format::eBc1RgbSrgbBlock;
        case Format::kBc1RgbaUnormBlock:
            return vk::Format::eBc1RgbaUnormBlock;
        case Format::kBc1RgbaSrgbBlock:
            return vk::Format::eBc1RgbaSrgbBlock;
        case Format::kBc2UnormBlock:
            return vk::Format::eBc2UnormBlock;
        case Format::kBc2SrgbBlock:
            return vk::Format::eBc2SrgbBlock;
        case Format::kBc3UnormBlock:
            return vk::Format::eBc3UnormBlock;
        case Format::kBc3SrgbBlock:
            return vk::Format::eBc3SrgbBlock;
        case Format::kBc4UnormBlock:
            return vk::Format::eBc4UnormBlock;
        case Format::kBc4SnormBlock:
            return vk::Format::eBc4SnormBlock;
        case Format::kBc5UnormBlock:
            return vk::Format::eBc5UnormBlock;
        case Format::kBc5SnormBlock:
            return vk::Format::eBc5SnormBlock;
        case Format::kBc6HUfloatBlock:
            return vk::Format::eBc6HUfloatBlock;
        case Format::kBc6HSfloatBlock:
            return vk::Format::eBc6HSfloatBlock;
        case Format::kBc7UnormBlock:
            return vk::Format::eBc7UnormBlock;
        case Format::kBc7SrgbBlock:
            return vk::Format::eBc7SrgbBlock;
        case Format::kEtc2R8G8B8UnormBlock:
            return vk::Format::eEtc2R8G8B8UnormBlock;
        case Format::kEtc2R8G8B8SrgbBlock:
            return vk::Format::eEtc2R8G8B8SrgbBlock;
        case Format::kEtc2R8G8B8A1UnormBlock:
            return vk::Format::eEtc2R8G8B8A1UnormBlock;
        case Format::kEtc2R8G8B8A1SrgbBlock:
            return vk::Format::eEtc2R8G8B8A1SrgbBlock;
        case Format::kEtc2R8G8B8A8UnormBlock:
            return vk::Format::eEtc2R8G8B8A8UnormBlock;
        case Format::kEtc2R8G8B8A8SrgbBlock:
            return vk::Format::eEtc2R8G8B8A8SrgbBlock;
        case Format::kEacR11UnormBlock:
            return vk::Format::eEacR11UnormBlock;
        case Format::kEacR11SnormBlock:
            return vk::Format::eEacR11SnormBlock;
        case Format::kEacR11G11UnormBlock:
            return vk::Format::eEacR11G11UnormBlock;
        case Format::kEacR11G11SnormBlock:
            return vk::Format::eEacR11G11SnormBlock;
        case Format::kAstc4x4UnormBlock:
            return vk::Format::eAstc4x4UnormBlock;
        case Format::kAstc4x4SrgbBlock:
            return vk::Format::eAstc4x4SrgbBlock;
        case Format::kAstc5x4UnormBlock:
            return vk::Format::eAstc5x4UnormBlock;
        case Format::kAstc5x4SrgbBlock:
            return vk::Format::eAstc5x4SrgbBlock;
        case Format::kAstc5x5UnormBlock:
            return vk::Format::eAstc5x5UnormBlock;
        case Format::kAstc5x5SrgbBlock:
            return vk::Format::eAstc5x5SrgbBlock;
        case Format::kAstc6x5UnormBlock:
            return vk::Format::eAstc6x5UnormBlock;
        case Format::kAstc6x5SrgbBlock:
            return vk::Format::eAstc6x5SrgbBlock;
        case Format::kAstc6x6UnormBlock:
            return vk::Format::eAstc6x6UnormBlock;
        case Format::kAstc6x6SrgbBlock:
            return vk::Format::eAstc6x6SrgbBlock;
        case Format::kAstc8x5UnormBlock:
            return vk::Format::eAstc8x5UnormBlock;
        case Format::kAstc8x5SrgbBlock:
            return vk::Format::eAstc8x5SrgbBlock;
        case Format::kAstc8x6UnormBlock:
            return vk::Format::eAstc8x6UnormBlock;
        case Format::kAstc8x6SrgbBlock:
            return vk::Format::eAstc8x6SrgbBlock;
        case Format::kAstc8x8UnormBlock:
            return vk::Format::eAstc8x8UnormBlock;
        case Format::kAstc8x8SrgbBlock:
            return vk::Format::eAstc8x8SrgbBlock;
        case Format::kAstc10x5UnormBlock:
            return vk::Format::eAstc10x5UnormBlock;
        case Format::kAstc10x5SrgbBlock:
            return vk::Format::eAstc10x5SrgbBlock;
        case Format::kAstc10x6UnormBlock:
            return vk::Format::eAstc10x6UnormBlock;
        case Format::kAstc10x6SrgbBlock:
            return vk::Format::eAstc10x6SrgbBlock;
        case Format::kAstc10x8UnormBlock:
            return vk::Format::eAstc10x8UnormBlock;
        case Format::kAstc10x8SrgbBlock:
            return vk::Format::eAstc10x8SrgbBlock;
        case Format::kAstc10x10UnormBlock:
            return vk::Format::eAstc10x10UnormBlock;
        case Format::kAstc10x10SrgbBlock:
            return vk::Format::eAstc10x10SrgbBlock;
        case Format::kAstc12x10UnormBlock:
            return vk::Format::eAstc12x10UnormBlock;
        case Format::kAstc12x10SrgbBlock:
            return vk::Format::eAstc12x10SrgbBlock;
        case Format::kAstc12x12UnormBlock:
            return vk::Format::eAstc12x12UnormBlock;
        case Format::kAstc12x12SrgbBlock:
            return vk::Format::eAstc12x12SrgbBlock;
        case Format::kPvrtc12BppUnormBlockIMG:
            return vk::Format::ePvrtc12BppUnormBlockIMG;
        case Format::kPvrtc14BppUnormBlockIMG:
            return vk::Format::ePvrtc14BppUnormBlockIMG;
        case Format::kPvrtc22BppUnormBlockIMG:
            return vk::Format::ePvrtc22BppUnormBlockIMG;
        case Format::kPvrtc24BppUnormBlockIMG:
            return vk::Format::ePvrtc24BppUnormBlockIMG;
        case Format::kPvrtc12BppSrgbBlockIMG:
            return vk::Format::ePvrtc12BppSrgbBlockIMG;
        case Format::kPvrtc14BppSrgbBlockIMG:
            return vk::Format::ePvrtc14BppSrgbBlockIMG;
        case Format::kPvrtc22BppSrgbBlockIMG:
            return vk::Format::ePvrtc22BppSrgbBlockIMG;
        case Format::kPvrtc24BppSrgbBlockIMG:
            return vk::Format::ePvrtc24BppSrgbBlockIMG;
        case Format::kR16G16Sfixed5NV:
            return vk::Format::eR16G16Sfixed5NV;
        case Format::kA1B5G5R5UnormPack16KHR:
            return vk::Format::eA1B5G5R5UnormPack16KHR;
        case Format::kA8UnormKHR:
            return vk::Format::eA8UnormKHR;
        default:
            throw std::invalid_argument("Unsupported format");
    }
}

inline vk::PolygonMode VulkanRenderer::Convert(PolygonMode mode) {
    switch (mode) {
        case PolygonMode::kFill:
            return vk::PolygonMode::eFill;
        case PolygonMode::kLine:
            return vk::PolygonMode::eLine;
        case PolygonMode::kPoint:
            return vk::PolygonMode::ePoint;
        default:
            throw std::runtime_error("Invalid PolygonMode.");
    }
}

inline vk::CullModeFlags VulkanRenderer::Convert(CullMode mode) {
    switch (mode) {
        case CullMode::kNone:
            return vk::CullModeFlagBits::eNone;
        case CullMode::kFront:
            return vk::CullModeFlagBits::eFront;
        case CullMode::kBack:
            return vk::CullModeFlagBits::eBack;
        case CullMode::kFrontAndBack:
            return vk::CullModeFlagBits::eFrontAndBack;
        default:
            throw std::runtime_error("Invalid CullMode.");
    }
}

inline vk::FrontFace VulkanRenderer::Convert(FrontFace face) {
    switch (face) {
        case FrontFace::kCcw:
            return vk::FrontFace::eCounterClockwise;
        case FrontFace::kCw:
            return vk::FrontFace::eClockwise;
        default:
            throw std::runtime_error("Invalid FrontFace.");
    }
}

inline vk::ImageUsageFlags VulkanRenderer::Convert(TextureUsage usage) {
    vk::ImageUsageFlags vk_usage = {};

    if ((usage & TextureUsage::kRenderTarget) == TextureUsage::kRenderTarget) {
        vk_usage |= vk::ImageUsageFlagBits::eColorAttachment;
    }
    if ((usage & TextureUsage::kDepthStencil) == TextureUsage::kDepthStencil) {
        vk_usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    }
    if ((usage & TextureUsage::kStorage) == TextureUsage::kStorage) {
        vk_usage |= vk::ImageUsageFlagBits::eStorage;
    }
    if ((usage & TextureUsage::kInputAttachment) == TextureUsage::kInputAttachment) {
        vk_usage |= vk::ImageUsageFlagBits::eInputAttachment;
    }

    return vk_usage;
}

inline vk::BufferUsageFlags VulkanRenderer::Convert(BufferUsage usage) {
    vk::BufferUsageFlags vk_usage = {};

    if (usage & BufferUsage::kVertex)
        vk_usage |= vk::BufferUsageFlagBits::eVertexBuffer;
    if (usage & BufferUsage::kIndex)
        vk_usage |= vk::BufferUsageFlagBits::eIndexBuffer;
    if (usage & BufferUsage::kUniform)
        vk_usage |= vk::BufferUsageFlagBits::eUniformBuffer;
    if (usage & BufferUsage::kStorage)
        vk_usage |= vk::BufferUsageFlagBits::eStorageBuffer;
    if (usage & BufferUsage::kIndirect)
        vk_usage |= vk::BufferUsageFlagBits::eIndirectBuffer;

    return vk_usage;
}

inline vk::AttachmentLoadOp VulkanRenderer::Convert(AttachmentLoadOp op) {
    switch (op) {
        case AttachmentLoadOp::kClear:
            return vk::AttachmentLoadOp::eClear;
        case AttachmentLoadOp::kLoad:
            return vk::AttachmentLoadOp::eLoad;
        case AttachmentLoadOp::kDontCare:
            return vk::AttachmentLoadOp::eDontCare;
        default:
            return vk::AttachmentLoadOp::eClear;
    }
}

inline vk::AttachmentStoreOp VulkanRenderer::Convert(AttachmentStoreOp op) {
    switch (op) {
        case AttachmentStoreOp::kStore:
            return vk::AttachmentStoreOp::eStore;
        case AttachmentStoreOp::kDontCare:
            return vk::AttachmentStoreOp::eDontCare;
        default:
            return vk::AttachmentStoreOp::eStore;
    }
}

inline vk::ShaderStageFlagBits VulkanRenderer::Convert(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::kVertex:
            return vk::ShaderStageFlagBits::eVertex;
        case ShaderStage::kFragment:
            return vk::ShaderStageFlagBits::eFragment;
        case ShaderStage::kCompute:
            return vk::ShaderStageFlagBits::eCompute;
        // Add other shader stages as needed
        default:
            return vk::ShaderStageFlagBits::eVertex;
    }
}

inline vk::SampleCountFlagBits VulkanRenderer::Convert(SampleCount sample) {
    switch (sample) {
        case SampleCount::k1:
            return vk::SampleCountFlagBits::e1;
        case SampleCount::k2:
            return vk::SampleCountFlagBits::e2;
        case SampleCount::k4:
            return vk::SampleCountFlagBits::e4;
        case SampleCount::k8:
            return vk::SampleCountFlagBits::e8;
        case SampleCount::k16:
            return vk::SampleCountFlagBits::e16;
        case SampleCount::k32:
            return vk::SampleCountFlagBits::e32;
        case SampleCount::k64:
            return vk::SampleCountFlagBits::e64;
        default:
            return vk::SampleCountFlagBits::e1;
    }
}

inline vk::PrimitiveTopology VulkanRenderer::Convert(Topology topology) {
    switch (topology) {
        case Topology::kPointList:
            return vk::PrimitiveTopology::ePointList;
        case Topology::kLineList:
            return vk::PrimitiveTopology::eLineList;
        case Topology::kLineStrip:
            return vk::PrimitiveTopology::eLineStrip;
        case Topology::kTriangleList:
            return vk::PrimitiveTopology::eTriangleList;
        case Topology::kTriangleStrip:
            return vk::PrimitiveTopology::eTriangleStrip;
        case Topology::kTriangleFan:
            return vk::PrimitiveTopology::eTriangleFan;
        default:
            return vk::PrimitiveTopology::eTriangleList;
    }
}

inline vk::Format VulkanRenderer::Convert(const spirv_cross::SPIRType& type) {
    if (type.basetype == spirv_cross::SPIRType::Float) {
        switch (type.vecsize) {
            case 1:
                return vk::Format::eR32Sfloat;
            case 2:
                return vk::Format::eR32G32Sfloat;
            case 3:
                return vk::Format::eR32G32B32Sfloat;
            case 4:
                return vk::Format::eR32G32B32A32Sfloat;
            default:
                throw std::runtime_error("Unsupported SPIRType vecsize for float.");
        }
    }
    // Handle other base types (Int, UInt, etc.) as needed
    throw std::runtime_error("Unsupported SPIRType basetype for reflection.");
}