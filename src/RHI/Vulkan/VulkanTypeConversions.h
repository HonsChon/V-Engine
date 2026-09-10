#pragma once

#include "RHITypes.h"
#include <vulkan/vulkan.h>
#include <stdexcept>

// =============================================================================
// RHI → Vulkan type conversions
// =============================================================================

namespace VulkanTypeConversions {

// ---- Format ----
inline VkFormat toVkFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::Undefined:            return VK_FORMAT_UNDEFINED;
        case RHIFormat::R8_UNORM:             return VK_FORMAT_R8_UNORM;
        case RHIFormat::R8G8_UNORM:           return VK_FORMAT_R8G8_UNORM;
        case RHIFormat::R8G8B8A8_UNORM:       return VK_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::R8G8B8A8_SRGB:        return VK_FORMAT_R8G8B8A8_SRGB;
        case RHIFormat::B8G8R8A8_UNORM:       return VK_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::B8G8R8A8_SRGB:        return VK_FORMAT_B8G8R8A8_SRGB;
        case RHIFormat::R16_SFLOAT:           return VK_FORMAT_R16_SFLOAT;
        case RHIFormat::R16G16_SFLOAT:        return VK_FORMAT_R16G16_SFLOAT;
        case RHIFormat::R16G16B16A16_SFLOAT:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case RHIFormat::R32_SFLOAT:           return VK_FORMAT_R32_SFLOAT;
        case RHIFormat::R32G32_SFLOAT:        return VK_FORMAT_R32G32_SFLOAT;
        case RHIFormat::R32G32B32_SFLOAT:     return VK_FORMAT_R32G32B32_SFLOAT;
        case RHIFormat::R32G32B32A32_SFLOAT:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case RHIFormat::R32_UINT:             return VK_FORMAT_R32_UINT;
        case RHIFormat::D16_UNORM:            return VK_FORMAT_D16_UNORM;
        case RHIFormat::D32_SFLOAT:           return VK_FORMAT_D32_SFLOAT;
        case RHIFormat::D24_UNORM_S8_UINT:    return VK_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D32_SFLOAT_S8_UINT:   return VK_FORMAT_D32_SFLOAT_S8_UINT;
        default: return VK_FORMAT_UNDEFINED;
    }
}

inline RHIFormat fromVkFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_UNDEFINED:             return RHIFormat::Undefined;
        case VK_FORMAT_R8_UNORM:              return RHIFormat::R8_UNORM;
        case VK_FORMAT_R8G8_UNORM:            return RHIFormat::R8G8_UNORM;
        case VK_FORMAT_R8G8B8A8_UNORM:        return RHIFormat::R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB:         return RHIFormat::R8G8B8A8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM:        return RHIFormat::B8G8R8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB:         return RHIFormat::B8G8R8A8_SRGB;
        case VK_FORMAT_R16_SFLOAT:            return RHIFormat::R16_SFLOAT;
        case VK_FORMAT_R16G16_SFLOAT:         return RHIFormat::R16G16_SFLOAT;
        case VK_FORMAT_R16G16B16A16_SFLOAT:   return RHIFormat::R16G16B16A16_SFLOAT;
        case VK_FORMAT_R32_SFLOAT:            return RHIFormat::R32_SFLOAT;
        case VK_FORMAT_R32G32_SFLOAT:         return RHIFormat::R32G32_SFLOAT;
        case VK_FORMAT_R32G32B32_SFLOAT:      return RHIFormat::R32G32B32_SFLOAT;
        case VK_FORMAT_R32G32B32A32_SFLOAT:   return RHIFormat::R32G32B32A32_SFLOAT;
        case VK_FORMAT_R32_UINT:              return RHIFormat::R32_UINT;
        case VK_FORMAT_D16_UNORM:             return RHIFormat::D16_UNORM;
        case VK_FORMAT_D32_SFLOAT:            return RHIFormat::D32_SFLOAT;
        case VK_FORMAT_D24_UNORM_S8_UINT:     return RHIFormat::D24_UNORM_S8_UINT;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:    return RHIFormat::D32_SFLOAT_S8_UINT;
        default: return RHIFormat::Undefined;
    }
}

// ---- Buffer Usage ----
inline VkBufferUsageFlags toVkBufferUsage(RHIBufferUsage usage) {
    VkBufferUsageFlags flags = 0;
    if (hasFlag(usage, RHIBufferUsage::Vertex))      flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (hasFlag(usage, RHIBufferUsage::Index))        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (hasFlag(usage, RHIBufferUsage::Uniform))      flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (hasFlag(usage, RHIBufferUsage::Storage))      flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (hasFlag(usage, RHIBufferUsage::Indirect))     flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (hasFlag(usage, RHIBufferUsage::TransferSrc))  flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, RHIBufferUsage::TransferDst))  flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}

// ---- Texture / Image Usage ----
inline VkImageUsageFlags toVkImageUsage(RHITextureUsage usage) {
    VkImageUsageFlags flags = 0;
    if (hasFlag(usage, RHITextureUsage::Sampled))                flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (hasFlag(usage, RHITextureUsage::Storage))                flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (hasFlag(usage, RHITextureUsage::ColorAttachment))        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (hasFlag(usage, RHITextureUsage::DepthStencilAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (hasFlag(usage, RHITextureUsage::TransferSrc))            flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, RHITextureUsage::TransferDst))            flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(usage, RHITextureUsage::InputAttachment))        flags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    return flags;
}

// ---- Shader Stage ----
inline VkShaderStageFlags toVkShaderStage(RHIShaderStage stage) {
    VkShaderStageFlags flags = 0;
    if (hasFlag(stage, RHIShaderStage::Vertex))      flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (hasFlag(stage, RHIShaderStage::Fragment))     flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (hasFlag(stage, RHIShaderStage::Compute))      flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (hasFlag(stage, RHIShaderStage::Geometry))     flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (hasFlag(stage, RHIShaderStage::TessControl))  flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (hasFlag(stage, RHIShaderStage::TessEval))     flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    return flags;
}

inline VkShaderStageFlagBits toVkShaderStageSingle(RHIShaderStage stage) {
    switch (stage) {
        case RHIShaderStage::Vertex:   return VK_SHADER_STAGE_VERTEX_BIT;
        case RHIShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case RHIShaderStage::Compute:  return VK_SHADER_STAGE_COMPUTE_BIT;
        case RHIShaderStage::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
        default: return VK_SHADER_STAGE_VERTEX_BIT;
    }
}

// ---- Memory Usage ----
inline VkMemoryPropertyFlags toVkMemoryProperties(RHIMemoryUsage usage) {
    switch (usage) {
        case RHIMemoryUsage::GPUOnly:  return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        case RHIMemoryUsage::CPUToGPU: return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        case RHIMemoryUsage::GPUToCPU: return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        default: return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
}

// ---- Descriptor Type ----
inline VkDescriptorType toVkDescriptorType(RHIDescriptorType type) {
    switch (type) {
        case RHIDescriptorType::UniformBuffer:         return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case RHIDescriptorType::StorageBuffer:         return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case RHIDescriptorType::CombinedImageSampler:  return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case RHIDescriptorType::SampledImage:          return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case RHIDescriptorType::StorageImage:          return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case RHIDescriptorType::Sampler:               return VK_DESCRIPTOR_TYPE_SAMPLER;
        case RHIDescriptorType::InputAttachment:       return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        case RHIDescriptorType::UniformBufferDynamic:  return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case RHIDescriptorType::StorageBufferDynamic:  return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        default: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

// ---- Compare Op ----
inline VkCompareOp toVkCompareOp(RHICompareOp op) {
    switch (op) {
        case RHICompareOp::Never:          return VK_COMPARE_OP_NEVER;
        case RHICompareOp::Less:           return VK_COMPARE_OP_LESS;
        case RHICompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
        case RHICompareOp::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case RHICompareOp::Greater:        return VK_COMPARE_OP_GREATER;
        case RHICompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
        case RHICompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case RHICompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_ALWAYS;
    }
}

// ---- Cull Mode ----
inline VkCullModeFlags toVkCullMode(RHICullMode mode) {
    switch (mode) {
        case RHICullMode::None:         return VK_CULL_MODE_NONE;
        case RHICullMode::Front:        return VK_CULL_MODE_FRONT_BIT;
        case RHICullMode::Back:         return VK_CULL_MODE_BACK_BIT;
        case RHICullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
        default: return VK_CULL_MODE_NONE;
    }
}

// ---- Front Face ----
inline VkFrontFace toVkFrontFace(RHIFrontFace face) {
    switch (face) {
        case RHIFrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        case RHIFrontFace::Clockwise:        return VK_FRONT_FACE_CLOCKWISE;
        default: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
}

// ---- Polygon Mode ----
inline VkPolygonMode toVkPolygonMode(RHIPolygonMode mode) {
    switch (mode) {
        case RHIPolygonMode::Fill:  return VK_POLYGON_MODE_FILL;
        case RHIPolygonMode::Line:  return VK_POLYGON_MODE_LINE;
        case RHIPolygonMode::Point: return VK_POLYGON_MODE_POINT;
        default: return VK_POLYGON_MODE_FILL;
    }
}

// ---- Topology ----
inline VkPrimitiveTopology toVkTopology(RHIPrimitiveTopology topo) {
    switch (topo) {
        case RHIPrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case RHIPrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case RHIPrimitiveTopology::TriangleFan:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case RHIPrimitiveTopology::LineList:       return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case RHIPrimitiveTopology::LineStrip:      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case RHIPrimitiveTopology::PointList:      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

// ---- Vertex Input Rate ----
inline VkVertexInputRate toVkVertexInputRate(RHIVertexInputRate rate) {
    switch (rate) {
        case RHIVertexInputRate::Vertex:   return VK_VERTEX_INPUT_RATE_VERTEX;
        case RHIVertexInputRate::Instance: return VK_VERTEX_INPUT_RATE_INSTANCE;
        default: return VK_VERTEX_INPUT_RATE_VERTEX;
    }
}

// ---- Index Type ----
inline VkIndexType toVkIndexType(RHIIndexType type) {
    switch (type) {
        case RHIIndexType::UInt16: return VK_INDEX_TYPE_UINT16;
        case RHIIndexType::UInt32: return VK_INDEX_TYPE_UINT32;
        default: return VK_INDEX_TYPE_UINT32;
    }
}

// ---- Load Op ----
inline VkAttachmentLoadOp toVkLoadOp(RHILoadOp op) {
    switch (op) {
        case RHILoadOp::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
        case RHILoadOp::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case RHILoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
}

// ---- Store Op ----
inline VkAttachmentStoreOp toVkStoreOp(RHIStoreOp op) {
    switch (op) {
        case RHIStoreOp::Store:    return VK_ATTACHMENT_STORE_OP_STORE;
        case RHIStoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        default: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
}

// ---- Image Layout ----
inline VkImageLayout toVkImageLayout(RHIImageLayout layout) {
    switch (layout) {
        case RHIImageLayout::Undefined:               return VK_IMAGE_LAYOUT_UNDEFINED;
        case RHIImageLayout::General:                  return VK_IMAGE_LAYOUT_GENERAL;
        case RHIImageLayout::ColorAttachment:          return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case RHIImageLayout::DepthStencilAttachment:   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case RHIImageLayout::DepthStencilReadOnly:     return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case RHIImageLayout::ShaderReadOnly:           return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RHIImageLayout::TransferSrc:              return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case RHIImageLayout::TransferDst:              return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case RHIImageLayout::PresentSrc:               return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        default: return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

// ---- Filter ----
inline VkFilter toVkFilter(RHIFilter filter) {
    switch (filter) {
        case RHIFilter::Nearest: return VK_FILTER_NEAREST;
        case RHIFilter::Linear:  return VK_FILTER_LINEAR;
        default: return VK_FILTER_LINEAR;
    }
}

// ---- Address Mode ----
inline VkSamplerAddressMode toVkAddressMode(RHIAddressMode mode) {
    switch (mode) {
        case RHIAddressMode::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case RHIAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case RHIAddressMode::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case RHIAddressMode::ClampToBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

// ---- Sample Count ----
inline VkSampleCountFlagBits toVkSampleCount(RHISampleCount count) {
    switch (count) {
        case RHISampleCount::Count1:  return VK_SAMPLE_COUNT_1_BIT;
        case RHISampleCount::Count2:  return VK_SAMPLE_COUNT_2_BIT;
        case RHISampleCount::Count4:  return VK_SAMPLE_COUNT_4_BIT;
        case RHISampleCount::Count8:  return VK_SAMPLE_COUNT_8_BIT;
        case RHISampleCount::Count16: return VK_SAMPLE_COUNT_16_BIT;
        case RHISampleCount::Count32: return VK_SAMPLE_COUNT_32_BIT;
        case RHISampleCount::Count64: return VK_SAMPLE_COUNT_64_BIT;
        default: return VK_SAMPLE_COUNT_1_BIT;
    }
}

// ---- Blend Factor ----
inline VkBlendFactor toVkBlendFactor(RHIBlendFactor factor) {
    switch (factor) {
        case RHIBlendFactor::Zero:             return VK_BLEND_FACTOR_ZERO;
        case RHIBlendFactor::One:              return VK_BLEND_FACTOR_ONE;
        case RHIBlendFactor::SrcColor:         return VK_BLEND_FACTOR_SRC_COLOR;
        case RHIBlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case RHIBlendFactor::DstColor:         return VK_BLEND_FACTOR_DST_COLOR;
        case RHIBlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case RHIBlendFactor::SrcAlpha:         return VK_BLEND_FACTOR_SRC_ALPHA;
        case RHIBlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case RHIBlendFactor::DstAlpha:         return VK_BLEND_FACTOR_DST_ALPHA;
        case RHIBlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        default: return VK_BLEND_FACTOR_ZERO;
    }
}

// ---- Blend Op ----
inline VkBlendOp toVkBlendOp(RHIBlendOp op) {
    switch (op) {
        case RHIBlendOp::Add:             return VK_BLEND_OP_ADD;
        case RHIBlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
        case RHIBlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case RHIBlendOp::Min:             return VK_BLEND_OP_MIN;
        case RHIBlendOp::Max:             return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

// ---- Color Component ----
inline VkColorComponentFlags toVkColorComponent(RHIColorComponent comp) {
    VkColorComponentFlags flags = 0;
    if (static_cast<uint32_t>(comp) & static_cast<uint32_t>(RHIColorComponent::R)) flags |= VK_COLOR_COMPONENT_R_BIT;
    if (static_cast<uint32_t>(comp) & static_cast<uint32_t>(RHIColorComponent::G)) flags |= VK_COLOR_COMPONENT_G_BIT;
    if (static_cast<uint32_t>(comp) & static_cast<uint32_t>(RHIColorComponent::B)) flags |= VK_COLOR_COMPONENT_B_BIT;
    if (static_cast<uint32_t>(comp) & static_cast<uint32_t>(RHIColorComponent::A)) flags |= VK_COLOR_COMPONENT_A_BIT;
    return flags;
}

// ---- Dynamic State ----
inline VkDynamicState toVkDynamicState(RHIDynamicState state) {
    switch (state) {
        case RHIDynamicState::Viewport:         return VK_DYNAMIC_STATE_VIEWPORT;
        case RHIDynamicState::Scissor:          return VK_DYNAMIC_STATE_SCISSOR;
        case RHIDynamicState::LineWidth:        return VK_DYNAMIC_STATE_LINE_WIDTH;
        case RHIDynamicState::DepthBias:        return VK_DYNAMIC_STATE_DEPTH_BIAS;
        case RHIDynamicState::BlendConstants:   return VK_DYNAMIC_STATE_BLEND_CONSTANTS;
        case RHIDynamicState::StencilReference: return VK_DYNAMIC_STATE_STENCIL_REFERENCE;
        default: return VK_DYNAMIC_STATE_VIEWPORT;
    }
}

// ---- Pipeline Stage ----
inline VkPipelineStageFlags toVkPipelineStage(RHIPipelineStage stage) {
    VkPipelineStageFlags flags = 0;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::TopOfPipe))             flags |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::VertexInput))           flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::VertexShader))          flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::FragmentShader))        flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::EarlyFragmentTests))    flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::LateFragmentTests))     flags |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::ColorAttachmentOutput)) flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::ComputeShader))         flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::Transfer))              flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::BottomOfPipe))          flags |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::AllGraphics))           flags |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(RHIPipelineStage::AllCommands))           flags |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    return flags;
}

// ---- Access Flags ----
inline VkAccessFlags toVkAccessFlags(RHIAccessFlags access) {
    VkAccessFlags flags = 0;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::IndirectCommandRead))         flags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::IndexRead))                   flags |= VK_ACCESS_INDEX_READ_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::VertexAttributeRead))         flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::UniformRead))                 flags |= VK_ACCESS_UNIFORM_READ_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::InputAttachmentRead))         flags |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::ShaderRead))                  flags |= VK_ACCESS_SHADER_READ_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::ShaderWrite))                 flags |= VK_ACCESS_SHADER_WRITE_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::ColorAttachmentRead))         flags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::ColorAttachmentWrite))        flags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::DepthStencilAttachmentRead))  flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::DepthStencilAttachmentWrite)) flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::TransferRead))                flags |= VK_ACCESS_TRANSFER_READ_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::TransferWrite))               flags |= VK_ACCESS_TRANSFER_WRITE_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::HostRead))                    flags |= VK_ACCESS_HOST_READ_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::HostWrite))                   flags |= VK_ACCESS_HOST_WRITE_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::MemoryRead))                  flags |= VK_ACCESS_MEMORY_READ_BIT;
    if (static_cast<uint32_t>(access) & static_cast<uint32_t>(RHIAccessFlags::MemoryWrite))                 flags |= VK_ACCESS_MEMORY_WRITE_BIT;
    return flags;
}

// ---- ClearValue ----
inline VkClearValue toVkClearValue(const RHIClearValue& val) {
    VkClearValue vk{};
    if (val.type == RHIClearValue::Type::Color) {
        vk.color = {{ val.color.r, val.color.g, val.color.b, val.color.a }};
    } else {
        vk.depthStencil = { val.depthStencil.depth, val.depthStencil.stencil };
    }
    return vk;
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

inline VkImageAspectFlags getAspectFlags(RHIFormat format) {
    if (isDepthFormat(format)) {
        VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencil(format)) flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
        return flags;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

} // namespace VulkanTypeConversions
