#pragma once

#include <cstdint>
#include <array>

// =============================================================================
// RHI Backend Selection
// =============================================================================

enum class RHIBackend {
    Vulkan,
    DX12,   // Reserved for future
};

// =============================================================================
// Pixel / Vertex Formats
// =============================================================================

enum class RHIFormat {
    Undefined = 0,

    // 8-bit per channel
    R8_UNORM,
    R8G8_UNORM,
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
    B8G8R8A8_UNORM,
    B8G8R8A8_SRGB,

    // 16-bit per channel
    R16_SFLOAT,
    R16G16_SFLOAT,
    R16G16B16A16_SFLOAT,

    // 32-bit per channel
    R32_SFLOAT,
    R32G32_SFLOAT,
    R32G32B32_SFLOAT,
    R32G32B32A32_SFLOAT,
    R32_UINT,

    // Depth / stencil
    D16_UNORM,
    D32_SFLOAT,
    D24_UNORM_S8_UINT,
    D32_SFLOAT_S8_UINT,
};

// =============================================================================
// Buffer Usage Flags (bitmask)
// =============================================================================

enum class RHIBufferUsage : uint32_t {
    None         = 0,
    Vertex       = 1 << 0,
    Index        = 1 << 1,
    Uniform      = 1 << 2,
    Storage      = 1 << 3,
    Indirect     = 1 << 4,
    TransferSrc  = 1 << 5,
    TransferDst  = 1 << 6,
};

// =============================================================================
// Texture / Image Usage Flags (bitmask)
// =============================================================================

enum class RHITextureUsage : uint32_t {
    None                    = 0,
    Sampled                 = 1 << 0,
    Storage                 = 1 << 1,
    ColorAttachment         = 1 << 2,
    DepthStencilAttachment  = 1 << 3,
    TransferSrc             = 1 << 4,
    TransferDst             = 1 << 5,
    InputAttachment         = 1 << 6,
};

// =============================================================================
// Shader Stage Flags (bitmask)
// =============================================================================

enum class RHIShaderStage : uint32_t {
    None        = 0,
    Vertex      = 1 << 0,
    Fragment    = 1 << 1,
    Compute     = 1 << 2,
    Geometry    = 1 << 3,
    TessControl = 1 << 4,
    TessEval    = 1 << 5,

    // Common combinations
    VertexFragment = Vertex | Fragment,
    All            = Vertex | Fragment | Compute | Geometry | TessControl | TessEval,
};

// =============================================================================
// Memory Usage
// =============================================================================

enum class RHIMemoryUsage {
    GPUOnly,      // DEVICE_LOCAL — for GPU-only resources (vertex/index/texture)
    CPUToGPU,     // HOST_VISIBLE | HOST_COHERENT — for uniform buffers, staging
    GPUToCPU,     // HOST_VISIBLE | HOST_CACHED  — for readback
};

// =============================================================================
// Descriptor (Binding) Types
// =============================================================================

enum class RHIDescriptorType {
    UniformBuffer,
    StorageBuffer,
    CombinedImageSampler,
    SampledImage,
    StorageImage,
    Sampler,
    InputAttachment,
    UniformBufferDynamic,
    StorageBufferDynamic,
};

// =============================================================================
// Pipeline Types
// =============================================================================

enum class RHIPipelineType {
    Graphics,
    Compute,
};

// =============================================================================
// Comparison Operations
// =============================================================================

enum class RHICompareOp {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

// =============================================================================
// Cull Mode
// =============================================================================

enum class RHICullMode {
    None,
    Front,
    Back,
    FrontAndBack,
};

// =============================================================================
// Front Face
// =============================================================================

enum class RHIFrontFace {
    CounterClockwise,
    Clockwise,
};

// =============================================================================
// Polygon Mode
// =============================================================================

enum class RHIPolygonMode {
    Fill,
    Line,
    Point,
};

// =============================================================================
// Topology
// =============================================================================

enum class RHIPrimitiveTopology {
    TriangleList,
    TriangleStrip,
    TriangleFan,
    LineList,
    LineStrip,
    PointList,
};

// =============================================================================
// Vertex Input Rate
// =============================================================================

enum class RHIVertexInputRate {
    Vertex,
    Instance,
};

// =============================================================================
// Index Type
// =============================================================================

enum class RHIIndexType {
    UInt16,
    UInt32,
};

// =============================================================================
// Load / Store Operations (for RenderPass attachments)
// =============================================================================

enum class RHILoadOp {
    Load,
    Clear,
    DontCare,
};

enum class RHIStoreOp {
    Store,
    DontCare,
};

// =============================================================================
// Image Layout
// =============================================================================

enum class RHIImageLayout {
    Undefined,
    General,
    ColorAttachment,
    DepthStencilAttachment,
    DepthStencilReadOnly,
    ShaderReadOnly,
    TransferSrc,
    TransferDst,
    PresentSrc,
};

// =============================================================================
// Filter (for samplers)
// =============================================================================

enum class RHIFilter {
    Nearest,
    Linear,
};

// =============================================================================
// Address Mode (for samplers)
// =============================================================================

enum class RHIAddressMode {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
};

// =============================================================================
// Blend Factor
// =============================================================================

enum class RHIBlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
};

// =============================================================================
// Blend Operation
// =============================================================================

enum class RHIBlendOp {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

// =============================================================================
// Dynamic State
// =============================================================================

enum class RHIDynamicState {
    Viewport,
    Scissor,
    LineWidth,
    DepthBias,
    BlendConstants,
    StencilReference,
};

// =============================================================================
// Sample Count
// =============================================================================

enum class RHISampleCount {
    Count1  = 1,
    Count2  = 2,
    Count4  = 4,
    Count8  = 8,
    Count16 = 16,
    Count32 = 32,
    Count64 = 64,
};

// =============================================================================
// Color Component Flags (bitmask)
// =============================================================================

enum class RHIColorComponent : uint32_t {
    None = 0,
    R    = 1 << 0,
    G    = 1 << 1,
    B    = 1 << 2,
    A    = 1 << 3,
    All  = R | G | B | A,
};

// =============================================================================
// Pipeline Stage Flags (bitmask, for barriers)
// =============================================================================

enum class RHIPipelineStage : uint32_t {
    TopOfPipe            = 1 << 0,
    VertexInput          = 1 << 1,
    VertexShader         = 1 << 2,
    FragmentShader       = 1 << 3,
    EarlyFragmentTests   = 1 << 4,
    LateFragmentTests    = 1 << 5,
    ColorAttachmentOutput= 1 << 6,
    ComputeShader        = 1 << 7,
    Transfer             = 1 << 8,
    BottomOfPipe         = 1 << 9,
    AllGraphics          = 1 << 10,
    AllCommands          = 1 << 11,
};

// =============================================================================
// Access Flags (bitmask, for barriers)
// =============================================================================

enum class RHIAccessFlags : uint32_t {
    None                        = 0,
    IndirectCommandRead         = 1 << 0,
    IndexRead                   = 1 << 1,
    VertexAttributeRead         = 1 << 2,
    UniformRead                 = 1 << 3,
    InputAttachmentRead         = 1 << 4,
    ShaderRead                  = 1 << 5,
    ShaderWrite                 = 1 << 6,
    ColorAttachmentRead         = 1 << 7,
    ColorAttachmentWrite        = 1 << 8,
    DepthStencilAttachmentRead  = 1 << 9,
    DepthStencilAttachmentWrite = 1 << 10,
    TransferRead                = 1 << 11,
    TransferWrite               = 1 << 12,
    HostRead                    = 1 << 13,
    HostWrite                   = 1 << 14,
    MemoryRead                  = 1 << 15,
    MemoryWrite                 = 1 << 16,
};

// =============================================================================
// Common Structures
// =============================================================================

struct RHIClearColorValue {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct RHIClearDepthStencilValue {
    float depth    = 1.0f;
    uint32_t stencil = 0;
};

struct RHIClearValue {
    enum class Type { Color, DepthStencil };
    Type type = Type::Color;
    RHIClearColorValue color;
    RHIClearDepthStencilValue depthStencil;

    static RHIClearValue Color(float r, float g, float b, float a = 1.0f) {
        RHIClearValue v;
        v.type = Type::Color;
        v.color = { r, g, b, a };
        return v;
    }
    static RHIClearValue DepthStencil(float depth = 1.0f, uint32_t stencil = 0) {
        RHIClearValue v;
        v.type = Type::DepthStencil;
        v.depthStencil = { depth, stencil };
        return v;
    }
};

struct RHIPushConstantRange {
    RHIShaderStage stageFlags = RHIShaderStage::None;
    uint32_t       offset     = 0;
    uint32_t       size       = 0;
};

struct RHIViewport {
    float x        = 0.0f;
    float y        = 0.0f;
    float width    = 0.0f;
    float height   = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct RHIScissor {
    int32_t  x      = 0;
    int32_t  y      = 0;
    uint32_t width  = 0;
    uint32_t height = 0;
};

struct RHIExtent2D {
    uint32_t width  = 0;
    uint32_t height = 0;
};

struct RHIImageBarrier {
    RHIPipelineStage srcStage   = RHIPipelineStage::TopOfPipe;
    RHIPipelineStage dstStage   = RHIPipelineStage::BottomOfPipe;
    RHIAccessFlags   srcAccess  = RHIAccessFlags::None;
    RHIAccessFlags   dstAccess  = RHIAccessFlags::None;
    RHIImageLayout   oldLayout  = RHIImageLayout::Undefined;
    RHIImageLayout   newLayout  = RHIImageLayout::General;
    // texture pointer will be set by caller
};

// =============================================================================
// Bitwise operator overloads — declared here, defined in a separate section
// =============================================================================

// -- RHIBufferUsage --
inline RHIBufferUsage operator|(RHIBufferUsage a, RHIBufferUsage b) {
    return static_cast<RHIBufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RHIBufferUsage operator&(RHIBufferUsage a, RHIBufferUsage b) {
    return static_cast<RHIBufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline RHIBufferUsage operator~(RHIBufferUsage a) {
    return static_cast<RHIBufferUsage>(~static_cast<uint32_t>(a));
}
inline RHIBufferUsage& operator|=(RHIBufferUsage& a, RHIBufferUsage b) { a = a | b; return a; }
inline RHIBufferUsage& operator&=(RHIBufferUsage& a, RHIBufferUsage b) { a = a & b; return a; }
inline bool hasFlag(RHIBufferUsage flags, RHIBufferUsage test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

// -- RHITextureUsage --
inline RHITextureUsage operator|(RHITextureUsage a, RHITextureUsage b) {
    return static_cast<RHITextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RHITextureUsage operator&(RHITextureUsage a, RHITextureUsage b) {
    return static_cast<RHITextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline RHITextureUsage operator~(RHITextureUsage a) {
    return static_cast<RHITextureUsage>(~static_cast<uint32_t>(a));
}
inline RHITextureUsage& operator|=(RHITextureUsage& a, RHITextureUsage b) { a = a | b; return a; }
inline RHITextureUsage& operator&=(RHITextureUsage& a, RHITextureUsage b) { a = a & b; return a; }
inline bool hasFlag(RHITextureUsage flags, RHITextureUsage test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

// -- RHIShaderStage --
inline RHIShaderStage operator|(RHIShaderStage a, RHIShaderStage b) {
    return static_cast<RHIShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RHIShaderStage operator&(RHIShaderStage a, RHIShaderStage b) {
    return static_cast<RHIShaderStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline RHIShaderStage operator~(RHIShaderStage a) {
    return static_cast<RHIShaderStage>(~static_cast<uint32_t>(a));
}
inline RHIShaderStage& operator|=(RHIShaderStage& a, RHIShaderStage b) { a = a | b; return a; }
inline RHIShaderStage& operator&=(RHIShaderStage& a, RHIShaderStage b) { a = a & b; return a; }
inline bool hasFlag(RHIShaderStage flags, RHIShaderStage test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

// -- RHIColorComponent --
inline RHIColorComponent operator|(RHIColorComponent a, RHIColorComponent b) {
    return static_cast<RHIColorComponent>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RHIColorComponent operator&(RHIColorComponent a, RHIColorComponent b) {
    return static_cast<RHIColorComponent>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline RHIColorComponent operator~(RHIColorComponent a) {
    return static_cast<RHIColorComponent>(~static_cast<uint32_t>(a));
}

// -- RHIPipelineStage --
inline RHIPipelineStage operator|(RHIPipelineStage a, RHIPipelineStage b) {
    return static_cast<RHIPipelineStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RHIPipelineStage operator&(RHIPipelineStage a, RHIPipelineStage b) {
    return static_cast<RHIPipelineStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline RHIPipelineStage operator~(RHIPipelineStage a) {
    return static_cast<RHIPipelineStage>(~static_cast<uint32_t>(a));
}

// -- RHIAccessFlags --
inline RHIAccessFlags operator|(RHIAccessFlags a, RHIAccessFlags b) {
    return static_cast<RHIAccessFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RHIAccessFlags operator&(RHIAccessFlags a, RHIAccessFlags b) {
    return static_cast<RHIAccessFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline RHIAccessFlags operator~(RHIAccessFlags a) {
    return static_cast<RHIAccessFlags>(~static_cast<uint32_t>(a));
}
