#pragma once

#include "RHIDescriptor.h"

#include <directx/d3d12.h>

class DX12RHIDevice;

// =============================================================================
// DX12RHIBindingLayout — analogous to VkDescriptorSetLayout.
// Stores the layout entries; the pipeline builder turns one layout into up to
// two root descriptor tables (resource table + sampler table) at space == set.
// =============================================================================

class DX12RHIBindingLayout : public RHIBindingLayout
{
public:
    DX12RHIBindingLayout(DX12RHIDevice* device, const RHIBindingLayoutDesc& desc);
    ~DX12RHIBindingLayout() override = default;

    const RHIBindingLayoutDesc& getDesc() const { return desc_; }

private:
    DX12RHIDevice*      device_;
    RHIBindingLayoutDesc desc_;
};

// =============================================================================
// DX12RHIBindingGroup — a contiguous block of shader-visible descriptors cut
// from the device descriptor rings. Resource descriptors (CBV/SRV/UAV) live in
// the CBV/SRV/UAV ring; sampler descriptors live in the sampler ring.
// =============================================================================

class DX12RHIBindingGroup : public RHIBindingGroup
{
public:
    DX12RHIBindingGroup(DX12RHIDevice* device, RHIBindingLayout* layout,
                        const RHIBindingGroupDesc& desc);
    ~DX12RHIBindingGroup() override = default;

    void updateBuffer(uint32_t binding, RHIBuffer* buffer,
                      uint64_t offset = 0, uint64_t range = 0) override;
    void updateTexture(uint32_t binding, RHITexture* texture,
                       RHISampler* sampler = nullptr) override;

    // Resource-block handles (CBV/SRV/UAV).
    D3D12_GPU_DESCRIPTOR_HANDLE getResourceGPUHandle() const { return resourceGpuBase_; }
    // Sampler-block handles (may be zeroed when the layout has no sampler entries).
    D3D12_GPU_DESCRIPTOR_HANDLE getSamplerGPUHandle() const { return samplerGpuBase_; }

    bool hasSamplerBlock() const { return samplerCount_ > 0; }

    D3D12_CPU_DESCRIPTOR_HANDLE getResourceCpuHandle() const { return resourceCpuBase_; }

private:
    /// @param outCpu  slot CPU handle (only valid when the entry has a resource slot)
    bool findBinding(uint32_t binding, RHIBindingEntry& outEntry,
                     bool& outIsSampler,
                     D3D12_CPU_DESCRIPTOR_HANDLE& outCpu) const;

    void writeBufferView(const RHIBindingEntry& entry,
                         D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                         RHIBuffer* buffer, uint64_t offset, uint64_t range);
    void writeTextureView(const RHIBindingEntry& entry,
                          D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                          RHITexture* texture, RHISampler* sampler);

    DX12RHIDevice* device_ = nullptr;

    std::vector<RHIBindingEntry> entries_;

    D3D12_CPU_DESCRIPTOR_HANDLE resourceCpuBase_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE resourceGpuBase_ = {};
    UINT                        resourceCount_ = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE samplerCpuBase_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE samplerGpuBase_ = {};
    UINT                        samplerCount_ = 0;

    UINT cbvSrvUavSize_ = 0;
    UINT samplerSize_   = 0;
};
