#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <variant>

// Forward declarations
class RHIBuffer;
class RHITexture;
class RHISampler;

// =============================================================================
// Binding Layout — describes what a set contains (Vulkan: DescriptorSetLayout)
// =============================================================================

struct RHIBindingEntry {
    uint32_t          binding        = 0;
    RHIDescriptorType type           = RHIDescriptorType::UniformBuffer;
    RHIShaderStage    stageFlags     = RHIShaderStage::All;
    uint32_t          count          = 1;   // array count
};

struct RHIBindingLayoutDesc {
    std::vector<RHIBindingEntry> entries;

    RHIBindingLayoutDesc& addBinding(uint32_t binding, RHIDescriptorType type,
                                      RHIShaderStage stages, uint32_t count = 1) {
        entries.push_back({ binding, type, stages, count });
        return *this;
    }
};

class RHIBindingLayout {
public:
    virtual ~RHIBindingLayout() = default;

    RHIBindingLayout(const RHIBindingLayout&) = delete;
    RHIBindingLayout& operator=(const RHIBindingLayout&) = delete;

protected:
    RHIBindingLayout() = default;
};

// =============================================================================
// Binding Group — actual resource bindings (Vulkan: DescriptorSet)
// =============================================================================

struct RHIBufferBinding {
    RHIBuffer* buffer = nullptr;
    uint64_t   offset = 0;
    uint64_t   range  = 0;  // 0 = whole buffer
};

struct RHITextureBinding {
    RHITexture* texture = nullptr;
    RHISampler* sampler = nullptr;
};

struct RHIBindingGroupEntry {
    uint32_t binding = 0;

    // One of these should be set
    RHIBufferBinding  bufferBinding;
    RHITextureBinding textureBinding;

    enum class Kind { Buffer, Texture } kind = Kind::Buffer;
};

struct RHIBindingGroupDesc {
    std::vector<RHIBindingGroupEntry> entries;

    RHIBindingGroupDesc& setBuffer(uint32_t binding, RHIBuffer* buffer,
                                    uint64_t offset = 0, uint64_t range = 0) {
        RHIBindingGroupEntry entry;
        entry.binding = binding;
        entry.kind = RHIBindingGroupEntry::Kind::Buffer;
        entry.bufferBinding = { buffer, offset, range };
        entries.push_back(entry);
        return *this;
    }

    RHIBindingGroupDesc& setTexture(uint32_t binding, RHITexture* texture,
                                     RHISampler* sampler = nullptr) {
        RHIBindingGroupEntry entry;
        entry.binding = binding;
        entry.kind = RHIBindingGroupEntry::Kind::Texture;
        entry.textureBinding = { texture, sampler };
        entries.push_back(entry);
        return *this;
    }
};

class RHIBindingGroup {
public:
    virtual ~RHIBindingGroup() = default;

    RHIBindingGroup(const RHIBindingGroup&) = delete;
    RHIBindingGroup& operator=(const RHIBindingGroup&) = delete;

protected:
    RHIBindingGroup() = default;
};
