/**
 * @file RHIAccelerationStructure.h
 * @brief 硬件光追加速结构抽象 — BLAS（底层，per-mesh）与 TLAS（顶层，per-instance）
 *
 * DX12 语义（DXR Tier 1.0+）：
 *   - BLAS 引用现有顶点/索引缓冲（不复制几何）
 *   - TLAS 引用一组 BLAS + 3x4 行主序变换矩阵
 *   - AS 结果资源由后端创建并持有；scratch 缓冲由调用方分配
 *     （尺寸从 getScratchSize() 查询，所有 BLAS 可共享一块 scratch）
 *
 * Vulkan 后端暂未实现（存根抛错），GPU 光追模式当前仅 DX12。
 */

#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <string>
#include <vector>

class RHIBuffer;
class RHIBLAS;
class RHITLAS;

/// 一个三角网格几何（BLAS 可以包含多个，本引擎每 mesh 一个 BLAS 单几何）
struct RHIRayTracingGeometryDesc {
    RHIBuffer*    vertexBuffer = nullptr;
    uint64_t      vertexOffset = 0;                          // 字节
    uint32_t      vertexStride = 0;                          // 字节（如 44 = pos/normal/uv/tangent）
    uint32_t      vertexCount  = 0;
    RHIFormat     vertexFormat = RHIFormat::R32G32B32_SFLOAT; // 位置通道

    RHIBuffer*    indexBuffer  = nullptr;                    // 可为 null（非索引）
    uint64_t      indexOffset  = 0;                          // 字节
    uint32_t      indexCount   = 0;
    RHIIndexType  indexType    = RHIIndexType::UInt32;
};

struct RHIBLASDesc {
    std::vector<RHIRayTracingGeometryDesc> geometries;
    bool        allowUpdate = false;    // 允许后续 refit（动态几何）
    std::string debugName;
};

/// TLAS 实例（对应 D3D12_RAYTRACING_INSTANCE_DESC / VkAccelerationStructureInstanceKHR）
struct RHIRayTracingInstance {
    RHIBLAS*    blas = nullptr;
    /// 3x4 行主序（DXR 约定：行向量 p' = p * M），已含世界变换
    float       transform[3][4] = {};
    uint32_t    instanceId = 0;           // shader 中 InstanceIndex()/InstanceID()
    uint32_t    hitGroupOffset = 0;       // ContributionToHitGroupIndex（SBT 偏移）
    bool        cullBackfaces = true;     // false = 双面
};

struct RHITLASDesc {
    std::vector<RHIRayTracingInstance> instances;
    bool        allowUpdate = false;
    std::string debugName;
};

class RHIBLAS {
public:
    virtual ~RHIBLAS() = default;

    /// AS 结果资源的 GPU 虚拟地址（TLAS 构建时引用）
    virtual uint64_t getGPUAddress() const = 0;
    /// 全量构建所需 scratch 大小（字节）
    virtual uint64_t getScratchSize() const = 0;
    /// 增量更新（refit）所需 scratch 大小（字节）
    virtual uint64_t getUpdateScratchSize() const = 0;

    RHIBLAS(const RHIBLAS&) = delete;
    RHIBLAS& operator=(const RHIBLAS&) = delete;

protected:
    RHIBLAS() = default;
};

class RHITLAS {
public:
    virtual ~RHITLAS() = default;

    virtual uint64_t getGPUAddress() const = 0;
    virtual uint64_t getScratchSize() const = 0;
    virtual uint64_t getUpdateScratchSize() const = 0;

    RHITLAS(const RHITLAS&) = delete;
    RHITLAS& operator=(const RHITLAS&) = delete;

protected:
    RHITLAS() = default;
};
