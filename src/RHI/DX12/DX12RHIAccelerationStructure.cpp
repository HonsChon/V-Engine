#include "DX12RHIAccelerationStructure.h"
#include "DX12RHIDevice.h"
#include "DX12RHIBuffer.h"
#include "DX12TypeConversions.h"

#include <stdexcept>

using namespace DX12TypeConversions;

namespace {

/// AS 资源尺寸需对齐到 D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT
uint64_t alignAS(uint64_t v) {
    constexpr uint64_t kAlign = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
    return (v + kAlign - 1) / kAlign * kAlign;
}

} // namespace

// =============================================================================
// DX12RHIBLAS
// =============================================================================

DX12RHIBLAS::DX12RHIBLAS(DX12RHIDevice* device, const RHIBLASDesc& desc)
    : device_(device)
    , geometries_(desc.geometries)
    , allowUpdate_(desc.allowUpdate)
{
    if (geometries_.empty()) {
        throw std::runtime_error("[DX12RHIBLAS] no geometries");
    }

    dxGeometries_.resize(geometries_.size());
    for (size_t i = 0; i < geometries_.size(); ++i) {
        const RHIRayTracingGeometryDesc& g = geometries_[i];
        auto* dxVB = static_cast<DX12RHIBuffer*>(g.vertexBuffer);
        if (!dxVB || g.vertexCount == 0) {
            throw std::runtime_error("[DX12RHIBLAS] invalid vertex geometry");
        }
        D3D12_RAYTRACING_GEOMETRY_DESC& dx = dxGeometries_[i];
        dx.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        dx.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;   // 无 any-hit 时全 opaque，最快路径
        dx.Triangles.VertexBuffer.StartAddress =
            dxVB->getGPUVirtualAddress() + g.vertexOffset;
        dx.Triangles.VertexBuffer.StrideInBytes = g.vertexStride;
        dx.Triangles.VertexCount = g.vertexCount;
        dx.Triangles.VertexFormat = toDXGIFormat(g.vertexFormat);
        if (g.indexBuffer) {
            auto* dxIB = static_cast<DX12RHIBuffer*>(g.indexBuffer);
            dx.Triangles.IndexBuffer = dxIB->getGPUVirtualAddress() + g.indexOffset;
            dx.Triangles.IndexCount = g.indexCount;
            dx.Triangles.IndexFormat = toD3D12IndexFormat(g.indexType);
        } else {
            dx.Triangles.IndexBuffer = 0;
            dx.Triangles.IndexCount = 0;
            dx.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
        }
        dx.Triangles.Transform3x4 = 0;
    }

    inputs_.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs_.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (allowUpdate_) {
        inputs_.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    }
    inputs_.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs_.pGeometryDescs = dxGeometries_.data();
    inputs_.NumDescs = static_cast<UINT>(dxGeometries_.size());

    // Prebuild info -> AS 结果资源
    Microsoft::WRL::ComPtr<ID3D12Device5> device5;
    if (FAILED(device->getDevice()->QueryInterface(IID_PPV_ARGS(&device5)))) {
        throw std::runtime_error("[DX12RHIBLAS] ID3D12Device5 unavailable (no DXR support?)");
    }
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs_, &info);
    if (info.ResultDataMaxSizeInBytes == 0) {
        throw std::runtime_error("[DX12RHIBLAS] prebuild info returned 0 size (geometry invalid?)");
    }
    scratchSize_ = alignAS(info.ScratchDataSizeInBytes);
    updateScratchSize_ = alignAS(info.UpdateScratchDataSizeInBytes);

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = alignAS(info.ResultDataMaxSizeInBytes);
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = device->getDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
        IID_PPV_ARGS(&resource_));
    if (FAILED(hr)) {
        throw std::runtime_error("[DX12RHIBLAS] failed to create AS resource");
    }
#ifndef NDEBUG
    if (!desc.debugName.empty()) {
        std::wstring wide(desc.debugName.begin(), desc.debugName.end());
        resource_->SetName(wide.c_str());
    }
#endif
}

void DX12RHIBLAS::fillBuildDesc(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& out,
                                D3D12_GPU_VIRTUAL_ADDRESS scratchVA, bool update) const {
    out = {};
    out.DestAccelerationStructureData = getGPUAddress();
    out.Inputs = inputs_;
    if (update) {
        out.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        out.SourceAccelerationStructureData = getGPUAddress();
    } else {
        out.SourceAccelerationStructureData = 0;
    }
    out.ScratchAccelerationStructureData = scratchVA;
}

// =============================================================================
// DX12RHITLAS
// =============================================================================

DX12RHITLAS::DX12RHITLAS(DX12RHIDevice* device, const RHITLASDesc& desc)
    : device_(device)
    , instanceCount_(static_cast<uint32_t>(desc.instances.size()))
    , allowUpdate_(desc.allowUpdate)
{
    if (instanceCount_ == 0) {
        throw std::runtime_error("[DX12RHITLAS] no instances");
    }

    inputs_.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs_.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (allowUpdate_) {
        inputs_.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    }
    inputs_.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs_.InstanceDescs = 0;   // build 时填充（GPU VA）
    inputs_.NumDescs = instanceCount_;

    Microsoft::WRL::ComPtr<ID3D12Device5> device5;
    if (FAILED(device->getDevice()->QueryInterface(IID_PPV_ARGS(&device5)))) {
        throw std::runtime_error("[DX12RHITLAS] ID3D12Device5 unavailable (no DXR support?)");
    }
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs_, &info);
    if (info.ResultDataMaxSizeInBytes == 0) {
        throw std::runtime_error("[DX12RHITLAS] prebuild info returned 0 size");
    }
    scratchSize_ = alignAS(info.ScratchDataSizeInBytes);
    updateScratchSize_ = alignAS(info.UpdateScratchDataSizeInBytes);

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = alignAS(info.ResultDataMaxSizeInBytes);
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = device->getDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
        IID_PPV_ARGS(&resource_));
    if (FAILED(hr)) {
        throw std::runtime_error("[DX12RHITLAS] failed to create AS resource");
    }
#ifndef NDEBUG
    if (!desc.debugName.empty()) {
        std::wstring wide(desc.debugName.begin(), desc.debugName.end());
        resource_->SetName(wide.c_str());
    }
#endif
}

void DX12RHITLAS::fillBuildDesc(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& out,
                                D3D12_GPU_VIRTUAL_ADDRESS instanceDataVA,
                                D3D12_GPU_VIRTUAL_ADDRESS scratchVA, bool update) const {
    out = {};
    out.DestAccelerationStructureData = getGPUAddress();
    out.Inputs = inputs_;
    out.Inputs.InstanceDescs = instanceDataVA;
    if (update) {
        out.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        out.SourceAccelerationStructureData = getGPUAddress();
    } else {
        out.SourceAccelerationStructureData = 0;
    }
    out.ScratchAccelerationStructureData = scratchVA;
}
