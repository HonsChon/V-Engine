/**
 * @file DX12RHIAccelerationStructure.h
 * @brief DX12/DXR 加速结构实现 — BLAS/TLAS 结果资源 + 构建描述
 *
 * AS 结果资源直接以 D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
 * 创建并终身保持该状态（DXR 惯例，不做状态轮转）。构建描述在
 * build 时由命令缓冲填充 GPU VA（scratch 每次可能不同）。
 */

#pragma once

#include "RHIAccelerationStructure.h"

#include <directx/d3d12.h>
#include <wrl/client.h>
#include <vector>

class DX12RHIDevice;
class DX12RHIBuffer;

class DX12RHIBLAS : public RHIBLAS {
public:
    DX12RHIBLAS(DX12RHIDevice* device, const RHIBLASDesc& desc);
    ~DX12RHIBLAS() override = default;

    uint64_t getGPUAddress() const override { return resource_->GetGPUVirtualAddress(); }
    uint64_t getScratchSize() const override { return scratchSize_; }
    uint64_t getUpdateScratchSize() const override { return updateScratchSize_; }

    /// 填充 DXR 构建描述（build 时由命令缓冲调用）
    void fillBuildDesc(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& out,
                       D3D12_GPU_VIRTUAL_ADDRESS scratchVA, bool update) const;

    const std::vector<RHIRayTracingGeometryDesc>& getGeometries() const { return geometries_; }
    bool allowUpdate() const { return allowUpdate_; }

private:
    DX12RHIDevice* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    std::vector<RHIRayTracingGeometryDesc> geometries_;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs_ = {};
    mutable std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> dxGeometries_;
    bool  allowUpdate_ = false;
    uint64_t scratchSize_ = 0;
    uint64_t updateScratchSize_ = 0;
};

class DX12RHITLAS : public RHITLAS {
public:
    DX12RHITLAS(DX12RHIDevice* device, const RHITLASDesc& desc);
    ~DX12RHITLAS() override = default;

    uint64_t getGPUAddress() const override { return resource_->GetGPUVirtualAddress(); }
    uint64_t getScratchSize() const override { return scratchSize_; }
    uint64_t getUpdateScratchSize() const override { return updateScratchSize_; }

    void fillBuildDesc(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& out,
                       D3D12_GPU_VIRTUAL_ADDRESS instanceDataVA,
                       D3D12_GPU_VIRTUAL_ADDRESS scratchVA, bool update) const;

    uint32_t getInstanceCount() const { return instanceCount_; }
    bool allowUpdate() const { return allowUpdate_; }

private:
    DX12RHIDevice* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs_ = {};
    uint32_t instanceCount_ = 0;
    bool  allowUpdate_ = false;
    uint64_t scratchSize_ = 0;
    uint64_t updateScratchSize_ = 0;
};
