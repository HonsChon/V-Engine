#include "DX12RHIDescriptor.h"
#include "DX12RHIDevice.h"
#include "DX12RHIBuffer.h"
#include "DX12RHITexture.h"
#include "DX12RHISampler.h"
#include "DX12TypeConversions.h"

#include <stdexcept>
#include <algorithm>

using namespace DX12TypeConversions;

// =============================================================================
// DX12RHIBindingLayout
// =============================================================================

DX12RHIBindingLayout::DX12RHIBindingLayout(DX12RHIDevice* device, const RHIBindingLayoutDesc& desc)
    : device_(device), desc_(desc)
{
}

// =============================================================================
// DX12RHIBindingGroup
// =============================================================================

namespace {

bool isSamplerRangeType(RHIDescriptorType type) {
    // Combined image samplers carry an implicit SamplerState in HLSL (s#), so
    // both explicit sampler bindings and combined samplers need a sampler slot.
    return type == RHIDescriptorType::Sampler ||
           type == RHIDescriptorType::CombinedImageSampler;
}

bool isResourceRangeType(RHIDescriptorType type) {
    return type != RHIDescriptorType::Sampler;
}

uint64_t roundUp256(uint64_t v) {
    return (v + 255u) / 256u * 256u;
}

} // namespace

DX12RHIBindingGroup::DX12RHIBindingGroup(DX12RHIDevice* device, RHIBindingLayout* layout,
                                         const RHIBindingGroupDesc& desc)
    : device_(device)
{
    auto* dxLayout = static_cast<DX12RHIBindingLayout*>(layout);
    entries_ = dxLayout->getDesc().entries;

    // Count descriptor needs.
    resourceCount_ = 0;
    samplerCount_ = 0;
    for (const auto& entry : entries_) {
        if (isResourceRangeType(entry.type)) {
            resourceCount_ += static_cast<UINT>(entry.count);
        }
        if (isSamplerRangeType(entry.type)) {
            samplerCount_ += static_cast<UINT>(entry.count);
        }
    }

    cbvSrvUavSize_ = device_->getDescriptorIncrement(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    samplerSize_   = device_->getDescriptorIncrement(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    if (resourceCount_ > 0) {
        // Binding groups live for the app lifetime: allocate as persistent so a
        // transient ring wrap never overwrites their descriptors.
        if (!device_->allocateResourceDescriptors(resourceCount_, &resourceCpuBase_, &resourceGpuBase_,
                                                  /*persistent=*/true)) {
            throw std::runtime_error("[DX12RHIBindingGroup] resource descriptor ring exhausted");
        }
    }
    if (samplerCount_ > 0) {
        if (!device_->allocateSamplerDescriptors(samplerCount_, &samplerCpuBase_, &samplerGpuBase_,
                                                 /*persistent=*/true)) {
            throw std::runtime_error("[DX12RHIBindingGroup] sampler descriptor ring exhausted");
        }
    }

    // Initial population from the create-time desc.
    for (const auto& e : desc.entries) {
        RHIBindingEntry entry;
        bool isSampler = false;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
        if (!findBinding(e.binding, entry, isSampler, cpu)) {
            continue;
        }
        if (isSampler) {
            if (e.kind == RHIBindingGroupEntry::Kind::Texture) {
                writeTextureView(entry, cpu, e.textureBinding.texture, e.textureBinding.sampler);
            }
            continue;
        }
        if (e.kind == RHIBindingGroupEntry::Kind::Buffer) {
            writeBufferView(entry, cpu, e.bufferBinding.buffer,
                            e.bufferBinding.offset, e.bufferBinding.range);
        } else {
            writeTextureView(entry, cpu, e.textureBinding.texture, e.textureBinding.sampler);
        }
    }
}

bool DX12RHIBindingGroup::findBinding(uint32_t binding, RHIBindingEntry& outEntry,
                                      bool& outIsSampler,
                                      D3D12_CPU_DESCRIPTOR_HANDLE& outCpu) const {
    UINT resourceSlot = 0;
    UINT samplerSlot = 0;
    for (const auto& entry : entries_) {
        if (entry.binding == binding) {
            outEntry = entry;
            outIsSampler = isSamplerRangeType(entry.type) && !isResourceRangeType(entry.type);
            // A combined-image-sampler entry occupies one resource slot and one sampler slot.
            if (entry.type == RHIDescriptorType::Sampler) {
                outIsSampler = true;
            }
            D3D12_CPU_DESCRIPTOR_HANDLE base = {};
            UINT index = 0;
            if (outIsSampler) {
                base = samplerCpuBase_;
                index = samplerSlot;
            } else {
                base = resourceCpuBase_;
                index = resourceSlot;
            }
            outCpu = base;
            outCpu.ptr += static_cast<SIZE_T>(index) * (outIsSampler ? samplerSize_ : cbvSrvUavSize_);
            return true;
        }
        if (isResourceRangeType(entry.type)) {
            resourceSlot += static_cast<UINT>(entry.count);
        }
        if (isSamplerRangeType(entry.type)) {
            samplerSlot += static_cast<UINT>(entry.count);
        }
    }
    return false;
}

void DX12RHIBindingGroup::updateBuffer(uint32_t binding, RHIBuffer* buffer,
                                       uint64_t offset, uint64_t range) {
    RHIBindingEntry entry;
    bool isSampler = false;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
    if (!findBinding(binding, entry, isSampler, cpu) || isSampler) {
        throw std::runtime_error("[DX12RHIBindingGroup] updateBuffer: binding is not a buffer binding");
    }
    if (!buffer) {
        return;
    }
    writeBufferView(entry, cpu, buffer, offset, range);
}

void DX12RHIBindingGroup::updateTexture(uint32_t binding, RHITexture* texture,
                                        RHISampler* sampler) {
    RHIBindingEntry entry;
    bool isSampler = false;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
    if (!findBinding(binding, entry, isSampler, cpu)) {
        return;
    }
    if (!texture && !sampler) {
        return;
    }

    if (entry.type == RHIDescriptorType::Sampler) {
        // Sampler-only binding.
        if (sampler) {
            auto* dxSampler = static_cast<DX12RHISampler*>(sampler);
            D3D12_SAMPLER_DESC samplerDesc = dxSampler->getD3D12SamplerDesc();
            device_->getDevice()->CreateSampler(&samplerDesc, cpu);
        }
        return;
    }

    // Resource slot (SRV/UAV).
    if (texture) {
        auto* dxTex = static_cast<DX12RHITexture*>(texture);
        D3D12_CPU_DESCRIPTOR_HANDLE resCpu = {};
        bool dummy = false;
        RHIBindingEntry resEntry;
        if (findBinding(binding, resEntry, dummy, resCpu)) {
            writeTextureView(entry, resCpu, texture, sampler);
        }
    }

    // Sampler slot of a combined image sampler.
    if (sampler && entry.type == RHIDescriptorType::CombinedImageSampler) {
        auto* dxSampler = static_cast<DX12RHISampler*>(sampler);
        D3D12_SAMPLER_DESC samplerDesc = dxSampler->getD3D12SamplerDesc();

        // Locate the sampler slot for this binding.
        UINT samplerSlot = 0;
        for (const auto& e : entries_) {
            if (e.binding == binding) {
                break;
            }
            if (isSamplerRangeType(e.type)) {
                samplerSlot += static_cast<UINT>(e.count);
            }
        }
        D3D12_CPU_DESCRIPTOR_HANDLE samCpu = samplerCpuBase_;
        samCpu.ptr += static_cast<SIZE_T>(samplerSlot) * samplerSize_;
        device_->getDevice()->CreateSampler(&samplerDesc, samCpu);
    }
}

// -----------------------------------------------------------------------------
// View writers
// -----------------------------------------------------------------------------

void DX12RHIBindingGroup::writeBufferView(const RHIBindingEntry& entry,
                                          D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                                          RHIBuffer* buffer, uint64_t offset, uint64_t range) {
    auto* dxBuf = static_cast<DX12RHIBuffer*>(buffer);
    ID3D12Resource* resource = dxBuf->getD3D12Resource();

    switch (entry.type) {
        case RHIDescriptorType::UniformBuffer:
        case RHIDescriptorType::UniformBufferDynamic: {
            if ((offset & 0xFF) != 0) {
                throw std::runtime_error("[DX12RHIBindingGroup] CBV offset must be 256-byte aligned");
            }
            const uint64_t remaining = dxBuf->getResourceSize() - offset;
            uint64_t size = (range == 0) ? remaining : std::min<uint64_t>(range, remaining);
            size = std::max<uint64_t>(roundUp256(size), 256);

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
            cbv.BufferLocation = dxBuf->getGPUVirtualAddress() + offset;
            cbv.SizeInBytes = static_cast<UINT>(std::min<uint64_t>(size, 0xFFFFFF00u));
            device_->getDevice()->CreateConstantBufferView(&cbv, cpu);
            break;
        }
        case RHIDescriptorType::StorageBuffer:
        case RHIDescriptorType::StorageBufferDynamic: {
            if (!hasFlag(dxBuf->getUsage(), RHIBufferUsage::Storage)) {
                throw std::runtime_error("[DX12RHIBindingGroup] storage buffer binding requires Storage usage "
                                        "(ALLOW_UNORDERED_ACCESS)");
            }
            const uint64_t remaining = dxBuf->getResourceSize() - offset;
            const uint64_t size = (range == 0) ? remaining : std::min<uint64_t>(range, remaining);

            D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
            uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.CounterOffsetInBytes = 0;
            uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

            if (dxBuf->desc_.structStride > 0) {
                // Structured buffer (RWStructuredBuffer).
                uav.Buffer.FirstElement = static_cast<UINT>(offset / dxBuf->desc_.structStride);
                uav.Buffer.NumElements = static_cast<UINT>(size / dxBuf->desc_.structStride);
                uav.Buffer.StructureByteStride = dxBuf->desc_.structStride;
            } else {
                // Raw view. spirv-cross lowers the engine's SSBOs to
                // RWByteAddressBuffer (raw loads/stores at byte offsets), which
                // D3D12 only accepts against a RAW UAV (R32_TYPELESS + RAW flag).
                uav.Format = DXGI_FORMAT_R32_TYPELESS;
                uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                uav.Buffer.FirstElement = static_cast<UINT>(offset / 4);
                uav.Buffer.NumElements = static_cast<UINT>(std::max<uint64_t>(size / 4, 1));
                uav.Buffer.StructureByteStride = 0;
            }
            device_->getDevice()->CreateUnorderedAccessView(resource, nullptr, &uav, cpu);
            break;
        }
        default:
            throw std::runtime_error("[DX12RHIBindingGroup] updateBuffer: unsupported descriptor type");
    }
}

void DX12RHIBindingGroup::writeTextureView(const RHIBindingEntry& entry,
                                           D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                                           RHITexture* texture, RHISampler* sampler) {
    (void)sampler;
    if (!texture) {
        return;
    }
    auto* dxTex = static_cast<DX12RHITexture*>(texture);
    ID3D12Device* d3d = device_->getDevice();

    switch (entry.type) {
        case RHIDescriptorType::SampledImage:
        case RHIDescriptorType::CombinedImageSampler:
        case RHIDescriptorType::InputAttachment: {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv = dxTex->getSRVDesc();
            d3d->CreateShaderResourceView(dxTex->getD3D12Resource(), &srv, cpu);
            break;
        }
        case RHIDescriptorType::StorageImage: {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav = dxTex->getUAVDesc();
            d3d->CreateUnorderedAccessView(dxTex->getD3D12Resource(), nullptr, &uav, cpu);
            break;
        }
        case RHIDescriptorType::Sampler: {
            if (sampler) {
                auto* dxSampler = static_cast<DX12RHISampler*>(sampler);
                D3D12_SAMPLER_DESC samplerDesc = dxSampler->getD3D12SamplerDesc();
                d3d->CreateSampler(&samplerDesc, cpu);
            }
            break;
        }
        default:
            throw std::runtime_error("[DX12RHIBindingGroup] updateTexture: unsupported descriptor type");
    }
}
