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

class RHITLAS;

struct RHIBindingGroupEntry {
    uint32_t binding = 0;

    // One of these should be set
    RHIBufferBinding  bufferBinding;
    RHITextureBinding textureBinding;
    RHITLAS*          accelerationStructure = nullptr;

    enum class Kind { Buffer, Texture, AccelerationStructure } kind = Kind::Buffer;
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

    RHIBindingGroupDesc& setAccelerationStructure(uint32_t binding, RHITLAS* tlas) {
        RHIBindingGroupEntry entry;
        entry.binding = binding;
        entry.kind = RHIBindingGroupEntry::Kind::AccelerationStructure;
        entry.accelerationStructure = tlas;
        entries.push_back(entry);
        return *this;
    }
};

class RHIBindingGroup {
public:
    virtual ~RHIBindingGroup() = default;

    // Update a single binding entry (buffer or texture) after creation
    virtual void updateBuffer(uint32_t binding, RHIBuffer* buffer,
                               uint64_t offset = 0, uint64_t range = 0) = 0;
    virtual void updateTexture(uint32_t binding, RHITexture* texture,
                                RHISampler* sampler = nullptr) = 0;
    /// 更新数组绑定（bindless）中的一个元素：binding 必须是 count>1 的
    /// SampledImageArray 条目，index < count。
    virtual void updateTextureArrayElement(uint32_t binding, uint32_t arrayIndex,
                                           RHITexture* texture) {
        (void)binding; (void)arrayIndex; (void)texture;   // 后端不支持的默认忽略
    }
    /// 更新加速结构绑定（TLAS 重建后）
    virtual void updateAccelerationStructure(uint32_t binding, RHITLAS* tlas) {
        (void)binding; (void)tlas;   // 后端不支持的默认忽略
    }

    RHIBindingGroup(const RHIBindingGroup&) = delete;
    RHIBindingGroup& operator=(const RHIBindingGroup&) = delete;

protected:
    RHIBindingGroup() = default;
};
