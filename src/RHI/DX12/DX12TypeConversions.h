#pragma once

#include "RHITypes.h"
#include <dxgi1_4.h>
#include <directx/d3d12.h>

// =============================================================================
// RHI -> DX12/DXGI type conversions
// =============================================================================

namespace DX12TypeConversions {

// ---- Format ----
inline DXGI_FORMAT toDXGIFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::Undefined:            return DXGI_FORMAT_UNKNOWN;
        case RHIFormat::R8_UNORM:             return DXGI_FORMAT_R8_UNORM;
        case RHIFormat::R8G8_UNORM:           return DXGI_FORMAT_R8G8_UNORM;
        case RHIFormat::R8G8B8A8_UNORM:       return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::R8G8B8A8_SRGB:        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHIFormat::B8G8R8A8_UNORM:       return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::B8G8R8A8_SRGB:        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RHIFormat::R16_SFLOAT:           return DXGI_FORMAT_R16_FLOAT;
        case RHIFormat::R16G16_SFLOAT:        return DXGI_FORMAT_R16G16_FLOAT;
        case RHIFormat::R16G16B16A16_SFLOAT:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RHIFormat::R32_SFLOAT:           return DXGI_FORMAT_R32_FLOAT;
        case RHIFormat::R32G32_SFLOAT:        return DXGI_FORMAT_R32G32_FLOAT;
        case RHIFormat::R32G32B32_SFLOAT:     return DXGI_FORMAT_R32G32B32_FLOAT;
        case RHIFormat::R32G32B32A32_SFLOAT:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHIFormat::R32_UINT:             return DXGI_FORMAT_R32_UINT;
        case RHIFormat::D16_UNORM:            return DXGI_FORMAT_D16_UNORM;
        case RHIFormat::D32_SFLOAT:           return DXGI_FORMAT_D32_FLOAT;
        case RHIFormat::D24_UNORM_S8_UINT:    return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D32_SFLOAT_S8_UINT:   return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

inline RHIFormat fromDXGIFormat(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_UNKNOWN:              return RHIFormat::Undefined;
        case DXGI_FORMAT_R8_UNORM:             return RHIFormat::R8_UNORM;
        case DXGI_FORMAT_R8G8_UNORM:           return RHIFormat::R8G8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM:       return RHIFormat::R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:  return RHIFormat::R8G8B8A8_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM:       return RHIFormat::B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:  return RHIFormat::B8G8R8A8_SRGB;
        case DXGI_FORMAT_R16_FLOAT:            return RHIFormat::R16_SFLOAT;
        case DXGI_FORMAT_R16G16_FLOAT:         return RHIFormat::R16G16_SFLOAT;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:   return RHIFormat::R16G16B16A16_SFLOAT;
        case DXGI_FORMAT_R32_FLOAT:            return RHIFormat::R32_SFLOAT;
        case DXGI_FORMAT_R32G32_FLOAT:         return RHIFormat::R32G32_SFLOAT;
        case DXGI_FORMAT_R32G32B32_FLOAT:      return RHIFormat::R32G32B32_SFLOAT;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:   return RHIFormat::R32G32B32A32_SFLOAT;
        case DXGI_FORMAT_R32_UINT:             return RHIFormat::R32_UINT;
        case DXGI_FORMAT_D16_UNORM:            return RHIFormat::D16_UNORM;
        case DXGI_FORMAT_D32_FLOAT:            return RHIFormat::D32_SFLOAT;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:    return RHIFormat::D24_UNORM_S8_UINT;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return RHIFormat::D32_SFLOAT_S8_UINT;
        default: return RHIFormat::Undefined;
    }
}

// ---- Buffer Usage -> D3D12 Resource Flags ----
inline D3D12_RESOURCE_FLAGS toD3D12ResourceFlags(RHIBufferUsage usage) {
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (hasFlag(usage, RHIBufferUsage::Storage)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    return flags;
}

// ---- Memory Usage -> D3D12 Heap Type ----
inline D3D12_HEAP_TYPE toD3D12HeapType(RHIMemoryUsage usage) {
    switch (usage) {
        case RHIMemoryUsage::GPUOnly:  return D3D12_HEAP_TYPE_DEFAULT;
        case RHIMemoryUsage::CPUToGPU: return D3D12_HEAP_TYPE_UPLOAD;
        case RHIMemoryUsage::GPUToCPU: return D3D12_HEAP_TYPE_READBACK;
        default: return D3D12_HEAP_TYPE_DEFAULT;
    }
}

// ---- Compare Op ----
inline D3D12_COMPARISON_FUNC toD3D12CompareFunc(RHICompareOp op) {
    switch (op) {
        case RHICompareOp::Never:          return D3D12_COMPARISON_FUNC_NEVER;
        case RHICompareOp::Less:           return D3D12_COMPARISON_FUNC_LESS;
        case RHICompareOp::Equal:          return D3D12_COMPARISON_FUNC_EQUAL;
        case RHICompareOp::LessOrEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case RHICompareOp::Greater:        return D3D12_COMPARISON_FUNC_GREATER;
        case RHICompareOp::NotEqual:       return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case RHICompareOp::GreaterOrEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case RHICompareOp::Always:         return D3D12_COMPARISON_FUNC_ALWAYS;
        default: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
}

// ---- Cull Mode ----
inline D3D12_CULL_MODE toD3D12CullMode(RHICullMode mode) {
    switch (mode) {
        case RHICullMode::None:  return D3D12_CULL_MODE_NONE;
        case RHICullMode::Front: return D3D12_CULL_MODE_FRONT;
        case RHICullMode::Back:  return D3D12_CULL_MODE_BACK;
        default: return D3D12_CULL_MODE_NONE;
    }
}

// ---- Topology ----
inline D3D12_PRIMITIVE_TOPOLOGY_TYPE toD3D12TopologyType(RHIPrimitiveTopology topo) {
    switch (topo) {
        case RHIPrimitiveTopology::TriangleList:
        case RHIPrimitiveTopology::TriangleStrip:
        case RHIPrimitiveTopology::TriangleFan:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        case RHIPrimitiveTopology::LineList:
        case RHIPrimitiveTopology::LineStrip:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case RHIPrimitiveTopology::PointList:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

inline D3D_PRIMITIVE_TOPOLOGY toD3DPrimitiveTopology(RHIPrimitiveTopology topo) {
    switch (topo) {
        case RHIPrimitiveTopology::TriangleList:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case RHIPrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case RHIPrimitiveTopology::LineList:       return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case RHIPrimitiveTopology::LineStrip:      return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case RHIPrimitiveTopology::PointList:      return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        default: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

// ---- Index Type -> DXGI_FORMAT ----
inline DXGI_FORMAT toD3D12IndexFormat(RHIIndexType type) {
    switch (type) {
        case RHIIndexType::UInt16: return DXGI_FORMAT_R16_UINT;
        case RHIIndexType::UInt32: return DXGI_FORMAT_R32_UINT;
        default: return DXGI_FORMAT_R32_UINT;
    }
}

// ---- Filter -> D3D12_FILTER ----
inline D3D12_FILTER toD3D12Filter(RHIFilter minFilter, RHIFilter magFilter, RHIFilter mipFilter) {
    // Encode: MIN_MAG_MIP combination
    bool minLinear = (minFilter == RHIFilter::Linear);
    bool magLinear = (magFilter == RHIFilter::Linear);
    bool mipLinear = (mipFilter == RHIFilter::Linear);

    if (minLinear && magLinear && mipLinear)  return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    if (minLinear && magLinear && !mipLinear) return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (!minLinear && !magLinear && !mipLinear) return D3D12_FILTER_MIN_MAG_MIP_POINT;
    // Fallback
    return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
}

// ---- Address Mode ----
inline D3D12_TEXTURE_ADDRESS_MODE toD3D12AddressMode(RHIAddressMode mode) {
    switch (mode) {
        case RHIAddressMode::Repeat:         return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case RHIAddressMode::MirroredRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case RHIAddressMode::ClampToEdge:    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case RHIAddressMode::ClampToBorder:  return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        default: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

// ---- Blend Factor ----
inline D3D12_BLEND toD3D12Blend(RHIBlendFactor factor) {
    switch (factor) {
        case RHIBlendFactor::Zero:             return D3D12_BLEND_ZERO;
        case RHIBlendFactor::One:              return D3D12_BLEND_ONE;
        case RHIBlendFactor::SrcColor:         return D3D12_BLEND_SRC_COLOR;
        case RHIBlendFactor::OneMinusSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
        case RHIBlendFactor::DstColor:         return D3D12_BLEND_DEST_COLOR;
        case RHIBlendFactor::OneMinusDstColor: return D3D12_BLEND_INV_DEST_COLOR;
        case RHIBlendFactor::SrcAlpha:         return D3D12_BLEND_SRC_ALPHA;
        case RHIBlendFactor::OneMinusSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
        case RHIBlendFactor::DstAlpha:         return D3D12_BLEND_DEST_ALPHA;
        case RHIBlendFactor::OneMinusDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
        default: return D3D12_BLEND_ZERO;
    }
}

// ---- Blend Op ----
inline D3D12_BLEND_OP toD3D12BlendOp(RHIBlendOp op) {
    switch (op) {
        case RHIBlendOp::Add:             return D3D12_BLEND_OP_ADD;
        case RHIBlendOp::Subtract:        return D3D12_BLEND_OP_SUBTRACT;
        case RHIBlendOp::ReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case RHIBlendOp::Min:             return D3D12_BLEND_OP_MIN;
        case RHIBlendOp::Max:             return D3D12_BLEND_OP_MAX;
        default: return D3D12_BLEND_OP_ADD;
    }
}

// ---- Helper: is format a depth format? ----
inline bool isDepthFormat(RHIFormat format) {
    return format == RHIFormat::D16_UNORM ||
           format == RHIFormat::D32_SFLOAT ||
           format == RHIFormat::D24_UNORM_S8_UINT ||
           format == RHIFormat::D32_SFLOAT_S8_UINT;
}

inline bool hasStencil(RHIFormat format) {
    return format == RHIFormat::D24_UNORM_S8_UINT ||
           format == RHIFormat::D32_SFLOAT_S8_UINT;
}

// ---- Helper: get the typeless format for depth (needed for SRV on depth textures) ----
inline DXGI_FORMAT toTypelessDepthFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::D16_UNORM:          return DXGI_FORMAT_R16_TYPELESS;
        case RHIFormat::D32_SFLOAT:         return DXGI_FORMAT_R32_TYPELESS;
        case RHIFormat::D24_UNORM_S8_UINT:  return DXGI_FORMAT_R24G8_TYPELESS;
        case RHIFormat::D32_SFLOAT_S8_UINT: return DXGI_FORMAT_R32G8X24_TYPELESS;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

} // namespace DX12TypeConversions
