#pragma once

/**
 * NaniteCluster.h - Nanite 风格�?Mesh Cluster 数据结构
 * 
 * Nanite 的核心思想是将大型网格分割成小�?Cluster（簇），
 * 每个 Cluster 包含�?64-128 个三角形，便于：
 * 1. GPU 上进行细粒度剔除
 * 2. 按需加载/卸载（Virtual Geometry�?
 * 3. 无缝 LOD 切换
 */

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

namespace Nanite {

// ============== 常量定义 ==============

// 每个 Cluster 的目标三角形数量
constexpr uint32_t CLUSTER_TARGET_TRIANGLES = 128;

// 每个 Cluster 的最大顶点数量（由于三角形共享顶点，通常小于 triangles * 3�?
constexpr uint32_t CLUSTER_MAX_VERTICES = 256;

// Cluster Group 中的最�?Cluster 数量（用�?LOD 层级�?
constexpr uint32_t CLUSTER_GROUP_MAX_SIZE = 8;

// ============== 数据结构 ==============

/**
 * GPU 端的 Cluster 数据（用�?Compute Shader 剔除�?
 * 必须�?shader 中的结构匹配
 * 注意：必须先定义，因�?Cluster 结构体会引用�?
 */
struct GPUClusterData {
    // 包围球（用于快速剔除）
    glm::vec4 boundingSphere;   // xyz: center, w: radius
    
    // AABB（用于精确剔除）
    glm::vec4 aabbMin;          // xyz: min, w: lodError
    glm::vec4 aabbMax;          // xyz: max, w: maxChildError
    
    // 法线锥（用于背面剔除�?
    glm::vec4 normalCone;       // xyz: axis, w: cos(angle)
    
    // 数据偏移
    uint32_t vertexOffset;      // 顶点数据偏移
    uint32_t indexOffset;       // 索引数据偏移
    uint32_t triangleCount;     // 三角形数�?
    uint32_t lodLevel;          // LOD 层级
    
    // 层级信息（用�?DAG 遍历�?
    uint32_t parentGroupIndex;  // �?Group 索引
    uint32_t flags;             // 标志�? bit0=enabled, bit1=isLeaf
    uint32_t childStartIndex;   // 子节点起始索�?
    uint32_t childCount;        // 子节点数�?
};

static_assert(sizeof(GPUClusterData) == 96, "GPUClusterData size mismatch");

/**
 * 压缩的顶点数�?
 * 使用量化来减少内存占�?
 */
struct PackedVertex {
    // 位置（相对于 Cluster 的局部包围盒，量化为 16 位）
    uint16_t posX, posY, posZ;
    uint16_t padding0;
    
    // 法线（八面体编码，量化为 16 位）
    int16_t normalX, normalY;
    
    // UV 坐标（量化为 16 位）
    uint16_t uvX, uvY;
};

/**
 * 未压缩的顶点数据（用于构建阶段）
 */
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;  // xyz: tangent, w: handedness
};

/**
 * Cluster 包围盒数�?
 */
struct ClusterBounds {
    glm::vec3 center;           // 包围球中�?
    float radius;               // 包围球半�?
    
    glm::vec3 aabbMin;          // AABB 最小点
    float lodError;             // LOD 误差（用�?LOD 选择�?
    
    glm::vec3 aabbMax;          // AABB 最大点
    float screenSizeThreshold;  // 屏幕尺寸阈�?
    
    // 法线锥（用于背面剔除优化�?
    glm::vec3 coneAxis;         // 法线锥轴�?
    float coneAngleCos;         // 法线锥角度的余弦（用�?cone culling�?
};

/**
 * Cluster - Nanite 的基本渲染单�?
 * 
 * 每个 Cluster 是独立可剔除的渲染单元，包含�?
 * - 一组三角形（约 64-128 个）
 * - 局部顶点数�?
 * - 包围盒信�?
 * - LOD 层级信息
 */
struct Cluster {
    // 索引数据（存储的是局部索引）
    // 注意：LOD 0 通常 <= 255 顶点，但高 LOD 合并后可能超过 255
    std::vector<uint32_t> localIndices;  // 三角形索引（每个三角形 3 个）
    
    // 顶点数据
    std::vector<Vertex> vertices;       // 未压缩顶�?
    std::vector<PackedVertex> packedVertices;  // 压缩顶点（GPU 使用�?
    
    // 包围信息
    ClusterBounds bounds;
    
    // 层级信息
    uint32_t lodLevel = 0;              // LOD 层级�? = 最高精度）
    uint32_t parentGroupIndex = ~0u;    // �?Cluster Group 索引
    uint32_t clusterGroupIndex = 0;     // 所�?Cluster Group 索引
    
    // 统计信息
    uint32_t triangleCount = 0;
    uint32_t vertexCount = 0;
    
    // GPU 数据偏移（在全局缓冲区中的位置）
    uint32_t gpuVertexOffset = 0;
    uint32_t gpuIndexOffset = 0;
    
    // GPU 端的 Cluster 数据（用�?Compute Shader�?
    GPUClusterData gpuData;
    
    // 计算包围�?
    void computeBounds();
    
    // 压缩顶点数据
    void packVertices();
    
    // 计算法线锥（用于背面剔除�?
    void computeNormalCone();
};

/**
 * ClusterGroup - Cluster 的层级组�?
 * 
 * 用于构建 LOD 层级�?
 * - 多个相邻�?Cluster 组成一�?Group
 * - 简化后生成更粗糙的 Cluster 作为父级
 */
struct ClusterGroup {
    std::vector<uint32_t> clusterIndices;  // �?Cluster 索引
    uint32_t parentClusterIndex = ~0u;     // 父（简化后的）Cluster 索引
    
    ClusterBounds bounds;                  // 整个 Group 的包围盒
    float maxChildError = 0.0f;            // �?Cluster 的最大误�?
    
    uint32_t lodLevel = 0;                 // 所�?LOD 层级
};

/**
 * ClusterizedMesh - 经过 Cluster 划分的网�?
 * 
 * 包含�?
 * - 所�?LOD 层级�?Clusters
 * - Cluster Groups（层级结构）
 * - GPU 数据引用
 */
class ClusterizedMesh {
public:
    ClusterizedMesh() = default;
    ~ClusterizedMesh() = default;
    
    // 所�?Clusters（包含所�?LOD 层级�?
    std::vector<Cluster> clusters;
    
    // Cluster Groups（层级结构）
    std::vector<ClusterGroup> clusterGroups;
    
    // 每个 LOD 层级�?Cluster 索引范围
    struct LODLevel {
        uint32_t clusterStartIndex;
        uint32_t clusterCount;
        float maxError;             // 该层级的最大几何误�?
    };
    std::vector<LODLevel> lodLevels;
    
    // 原始网格信息
    uint32_t originalTriangleCount = 0;
    uint32_t originalVertexCount = 0;
    std::string sourceMeshName;
    
    // GPU 数据
    uint32_t gpuClusterDataOffset = 0;  // 在全局 Cluster 缓冲区中的偏�?
    
    // 获取统计信息
    uint32_t getTotalTriangleCount() const;
    uint32_t getTotalClusterCount() const;
    uint32_t getLODCount() const { return static_cast<uint32_t>(lodLevels.size()); }
    
    // 根据屏幕误差选择合适的 LOD
    uint32_t selectLODLevel(float screenSpaceError) const;
};

} // namespace Nanite
