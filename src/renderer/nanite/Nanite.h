#pragma once

/**
 * Nanite.h - Nanite 系统的统一入口
 * 
 * Nanite �?Epic Games �?UE5 中引入的虚拟几何体系统，
 * 实现了：
 * - 网格自动 LOD
 * - 细粒�?GPU 剔除
 * - 按需几何体流式加�?
 * 
 * 本实现是学习目的的简化版本，包含核心概念�?
 * 1. Mesh Cluster �?- 将网格分割成小的渲染单元
 * 2. GPU Cluster 剔除 - 视锥、背面、遮挡剔�?
 * 3. 层级 LOD - 基于屏幕误差的自�?LOD 选择
 */

// 核心数据结构
#include "NaniteCluster.h"

// 网格处理
#include "MeshClusterizer.h"

namespace Nanite {

/**
 * Nanite 系统版本
 */
constexpr uint32_t VERSION_MAJOR = 1;
constexpr uint32_t VERSION_MINOR = 0;

/**
 * Nanite 系统配置
 */
struct NaniteConfig {
    // 是否启用 Cluster 剔除
    bool enableClusterCulling = true;
    
    // 是否启用法线锥背面剔�?
    bool enableConeCulling = true;
    
    // 是否启用 HZB 遮挡剔除（需�?HZB pass�?
    bool enableOcclusionCulling = false;
    
    // 屏幕误差阈值（像素）
    // 当几何体在屏幕上的误差小于此值时，使用更粗糙的 LOD
    float screenSpaceErrorThreshold = 1.0f;
    
    // LOD 误差缩放因子
    // 用于调整 LOD 选择的敏感度，值越大越倾向于使用低 LOD
    // 对于小模型（QEM 误差很小），需要较大的 scale 来让 LOD 切换在合理距离发生
    float lodErrorScale = 300.0f;
    
    // 是否启用调试可视�?
    bool debugVisualization = false;
    
    // 调试：显�?Cluster 边界
    bool debugShowClusterBounds = false;
    
    // 调试：使�?Cluster ID 着�?
    bool debugClusterColors = false;
};

/**
 * Nanite 统计信息（每帧）
 */
struct NaniteStats {
    uint32_t totalClusters = 0;         // �?Cluster 数量
    uint32_t visibleClusters = 0;       // 可见 Cluster 数量
    uint32_t culledByFrustum = 0;       // 被视锥剔除的数量
    uint32_t culledByCone = 0;          // 被法线锥剔除的数�?
    uint32_t culledByOcclusion = 0;     // 被遮挡剔除的数量
    
    uint32_t totalTriangles = 0;        // 总三角形数量
    uint32_t renderedTriangles = 0;     // 实际渲染的三角形数量
    
    float cullingTimeMs = 0.0f;         // GPU 剔除耗时
    float renderTimeMs = 0.0f;          // 渲染耗时
    
    // 计算剔除效率
    float getCullingEfficiency() const {
        return totalClusters > 0 
            ? 1.0f - (static_cast<float>(visibleClusters) / totalClusters)
            : 0.0f;
    }
};

} // namespace Nanite
