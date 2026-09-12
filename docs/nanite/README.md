# Nanite 虚拟几何系统实现文档

[TOC]



## 概述

本模块实现了类似 UE5 Nanite 的虚拟几何系统（Virtual Geometry System）。核心思想是将网格划分为小的 Cluster（簇），每个 Cluster 包含约 128 个三角形，支持细粒度的 GPU 剔除和 LOD 选择。

## 系统架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        Nanite Virtual Geometry                          │
│                                                                         │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │                    CPU: Mesh Processing                           │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌──────────┐ │  │
│  │  │   Mesh      │  │  Triangle   │  │   Greedy    │  │  Vertex  │ │  │
│  │  │   Input     │→ │  Adjacency  │→ │ Clustering  │→ │ Packing  │ │  │
│  │  │             │  │   Graph     │  │             │  │          │ │  │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └──────────┘ │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                  ↓                                      │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │                    GPU: Storage Buffers                           │  │
│  │  ┌─────────────────┐  ┌───────────────┐  ┌────────────────────┐  │  │
│  │  │  ClusterBuffer  │  │  VertexBuffer │  │    IndexBuffer     │  │  │
│  │  │  (GPUClusterData)│  │  (PackedVertex)│  │  (Local Indices)   │  │  │
│  │  └─────────────────┘  └───────────────┘  └────────────────────┘  │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                  ↓                                      │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │                 GPU: Cluster Culling Pass                         │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌───────────────────────────┐ │  │
│  │  │   Frustum   │  │   Normal    │  │  Visible Index Output     │ │  │
│  │  │   Culling   │→ │ Cone Cull   │→ │  (Compacted Draw List)    │ │  │
│  │  └─────────────┘  └─────────────┘  └───────────────────────────┘ │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                  ↓                                      │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │               Indirect / Visibility Draw                          │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

## 核心概念

### 1. Cluster（簇）

Cluster 是 Nanite 的基本剔除和渲染单元。每个 Cluster：
- 包含约 **128 个三角形**（可配置）
- 拥有独立的 **包围球** 和 **AABB**
- 存储 **法线锥（Normal Cone）** 用于背面剔除
- 支持独立的 **LOD 等级**

### 2. 顶点量化

为减少内存带宽，顶点位置使用 **16-bit 量化**：
- 位置坐标映射到 Cluster 局部 AABB 的 `[0, 1]` 范围
- 存储为 `uint16_t`，运行时解压到 `[aabbMin, aabbMax]`
- 法线使用八面体编码（2 x 16-bit）
- 纹理坐标量化为 16-bit

### 3. 法线锥（Normal Cone）

法线锥是所有三角形法线的包围锥：
- **轴（Axis）**: 锥轴方向（归一化）
- **半角余弦（cos(angle)）**: 定义锥的张角

当相机方向与锥轴的夹角小于 `(90° - halfAngle)` 时，整个 Cluster 可背面剔除。

---

## 文档目录

| 文档 | 描述 |
|------|------|
| [MeshClusterizer.md](MeshClusterizer.md) | 网格分簇算法详解 (METIS 多级图分区) |
| 本文档 | Nanite 系统概述与数据结构 |

---

## 核心组件

### 1. NaniteCluster.h - 数据结构定义

```cpp
// GPU 端 Cluster 数据结构 (96 bytes, 对齐到 std430)
struct GPUClusterData {
    glm::vec4 boundingSphere;  // xyz: center, w: radius
    glm::vec4 aabbMin;         // xyz: min, w: lodError (累积 QEM 误差)
    glm::vec4 aabbMax;         // xyz: max, w: maxChildError
    glm::vec4 normalCone;      // xyz: axis, w: cos(halfAngle)
    
    uint32_t meshIndex;        // 所属 mesh 索引 (索引 TransformBuffer)
    uint32_t indexOffset;      // 在全局索引缓冲中的偏移
    uint32_t triangleCount;    // 三角形数量
    uint32_t lodLevel;         // LOD 等级 (0 = 最精细)
    
    uint32_t parentGroupIndex; // 父节点索引 (0xFFFFFFFF = 根)
    uint32_t flags;            // bit0=enabled, bit1=isLeaf
    uint32_t childStartIndex;  // 子节点起始索引
    uint32_t childCount;       // 子节点数量
};

// 量化后的顶点数据 (16 bytes)
struct PackedVertex {
    uint16_t position[3];  // 量化位置
    uint16_t normalXY;     // 八面体编码法线 (前16位)
    uint16_t normalZ_u;    // 八面体编码法线 (后16位) + UV.x
    uint16_t v;            // UV.y
    uint16_t padding[2];   // 对齐到 16 bytes
};
```

### 2. MeshClusterizer.h/cpp - 网格分簇器

**功能：** 将标准网格划分为多个 Cluster。

**算法流程：**

```
1. 构建三角形邻接图
   ┌────────────────────────────────────────┐
   │  For each edge (v0, v1):               │
   │    EdgeKey = (min(v0,v1), max(v0,v1))  │
   │    Map[EdgeKey].push(triangleIndex)    │
   └────────────────────────────────────────┘

2. 贪心聚类
   ┌────────────────────────────────────────┐
   │  While (有未分配的三角形):             │
   │    创建新 Cluster                       │
   │    将种子三角形加入 Cluster             │
   │    While (Cluster 未满):                │
   │      从优先队列中取出最优邻居           │
   │      添加到 Cluster                     │
   │      将邻居的邻居加入候选队列           │
   └────────────────────────────────────────┘

3. 优先级计算
   ┌────────────────────────────────────────┐
   │  Priority = α * spatialProximity       │
   │           + β * normalSimilarity       │
   │                                         │
   │  spatialProximity: 质心距离的倒数       │
   │  normalSimilarity: 法线夹角余弦         │
   └────────────────────────────────────────┘
```

**核心代码：**

```cpp
std::vector<std::unique_ptr<Cluster>> MeshClusterizer::clusterize(
    const std::vector<float>& positions,
    const std::vector<uint32_t>& indices,
    const std::vector<float>& normals,
    const std::vector<float>& uvs)
{
    // 1. 构建三角形和邻接关系
    buildTrianglesAndAdjacency(positions, indices, normals, uvs);
    
    // 2. 执行贪心聚类
    greedyClustering();
    
    // 3. 为每个 Cluster 计算元数据
    for (auto& cluster : clusters) {
        computeClusterBounds(cluster.get());      // 包围盒/球
        computeNormalCone(cluster.get());         // 法线锥
        packClusterVertices(cluster.get());       // 量化顶点
    }
    
    return std::move(clusters);
}
```

### 3. NaniteManager.h/cpp - 全局管理器

**职责：**
- 管理所有已处理网格的 Cluster 数据
- 维护全局 GPU 缓冲区
- 调度 Culling Compute Pass

**缓冲区布局：**

```
ClusterBuffer (SSBO)
┌────────────────────────────────────────────────────┐
│ GPUClusterData[0] │ GPUClusterData[1] │ ... │      │
│   Mesh A Cluster0 │   Mesh A Cluster1 │     │      │
└────────────────────────────────────────────────────┘

VertexBuffer (SSBO)
┌────────────────────────────────────────────────────┐
│ PackedVertex[] for Cluster0 │ PackedVertex[] for C1│
└────────────────────────────────────────────────────┘

IndexBuffer (SSBO)
┌────────────────────────────────────────────────────┐
│ Indices for Cluster0 │ Indices for Cluster1 │ ... │
└────────────────────────────────────────────────────┘

VisibleClusterBuffer (SSBO, Output)
┌────────────────────────────────────────────────────┐
│ Counter │ VisibleClusterIndex[0] │ Index[1] │ ...  │
└────────────────────────────────────────────────────┘
```

**API 使用：**

```cpp
// 初始化
naniteManager = std::make_unique<NaniteManager>(device);
naniteManager->init(config);

// 处理网格
uint32_t meshIndex = naniteManager->processMesh(vertices, indices, normals, uvs);
std::cout << "Created " << naniteManager->getClusterCount(meshIndex) << " clusters\n";

// 上传到 GPU
naniteManager->uploadToGPU();

// 每帧执行剔除
naniteManager->performCulling(cmdBuffer, camera);
```

---

## GPU Shader

### cluster_culling.comp - Cluster 剔除 + DAG LOD 选择

着色器一次 dispatch 完成三件事：世界变换、Nanite DAG LOD 选择、视锥/法线锥剔除，
输出全局可见 cluster 索引列表（CPU 回读后逐 cluster 绘制）。

```glsl
#version 450
layout(local_size_x = 64) in;

layout(set = 0, binding = 0) uniform CullingUniforms {
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 viewProjMatrix;
    vec4 frustumPlanes[6];
    vec4 cameraPosition;        // xyz: position
    uvec4 clusterCountPacked;   // x=count, y=frustumCull, z=coneCull, w=lodSelect
    vec4 screenParams;          // x=width, y=height, z=lodErrorScale, w=pixelThreshold
} uniforms;

layout(set = 0, binding = 1) buffer ClusterBuffer  { ClusterData clusters[]; };
layout(set = 0, binding = 2) buffer TransformBuffer{ mat4 transforms[]; };  // 每 mesh 世界矩阵
layout(set = 0, binding = 3) writeonly buffer VisibleBuffer { uint visibleClusterIndices[]; };
layout(set = 0, binding = 4) buffer CounterBuffer  { uint visibleClusterCount; };
layout(set = 0, binding = 5) buffer SelectionStateBuffer { uint selectionState[]; };

float computeScreenSpaceError(float lodError, float distanceToCamera) {
    // abs(): Vulkan 投影做了 Y 翻转 (proj[1][1] < 0)
    float projScaleY = abs(uniforms.projMatrix[1][1]);
    return (lodError * projScaleY * SCREEN_HEIGHT * 0.5) / distanceToCamera
         * LOD_ERROR_SCALE;
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= CLUSTER_COUNT) return;

    ClusterData cluster = clusters[idx];
    if ((cluster.flags & 1u) == 0u) return;

    // 0. 世界空间变换（每个 mesh 一个矩阵）
    mat4 world = transforms[cluster.meshIndex];
    vec3 center = (world * vec4(cluster.boundingSphere.xyz, 1.0)).xyz;
    float dist = length(center - uniforms.cameraPosition.xyz);

    // 1. Nanite DAG LOD 选择：selfError <= T < parentError
    if (ENABLE_LOD_SELECTION) {
        float selfError = computeScreenSpaceError(cluster.aabbMin.w, dist);

        float parentError = 1e30;   // 根节点视为"父误差无穷大"
        uint parentIndex = cluster.parentGroupIndex;
        if (parentIndex != 0xFFFFFFFFu && parentIndex < CLUSTER_COUNT) {
            ClusterData parent = clusters[parentIndex];
            vec3 pc = (transforms[parent.meshIndex] * vec4(parent.boundingSphere.xyz, 1.0)).xyz;
            parentError = computeScreenSpaceError(parent.aabbMin.w,
                                                  length(pc - uniforms.cameraPosition.xyz));
        }
        if (!(selfError <= PIXEL_THRESHOLD && parentError > PIXEL_THRESHOLD)) return;
    }

    // 2. 视锥剔除（包围球）
    if (ENABLE_FRUSTUM_CULL && !frustumCullSphere(center, cluster.boundingSphere.w)) return;

    // 3. 法线锥背面剔除
    if (ENABLE_CONE_CULL && !coneCull(cluster.normalCone.xyz, cluster.normalCone.w,
                                      center, uniforms.cameraPosition.xyz)) return;

    // 4. 写入可见列表
    uint outIdx = atomicAdd(visibleClusterCount, 1);
    visibleClusterIndices[outIdx] = idx;
}
```

**LOD 选择规则**：每条"根→叶"路径上恰好命中一个节点（`self` 可接受且 `parent` 过粗），
因此不会出现镂空或同区域多 LOD 重叠。LOD0 叶节点 `lodError = 0`，近处必然命中；
根节点无父节点，远处必然兜底。父误差取父节点自身的累积 `lodError`
（`qemSelf + maxChildError`），距离用父节点包围球中心计算。

**调参**：`screenSpaceErrorThreshold`（像素阈值）与 `lodErrorScale`（QEM 误差放大系数）
可在 F1 Debug Panel 的 "Nanite (Cluster Vis)" 区域实时调整。

**lodError 语义**：父节点 `lodError = qemSelf + maxChildError`，必须是**纯几何 QEM 误差**。
`MeshSimplifier` 内部用含惩罚的折叠代价排序（边界边 +100、锁定顶点 +50），
但误差统计必须用不含惩罚的 `geometricError`；否则惩罚会污染整级误差（变成 50~100），
这些区域将永远无法选中 L1+（只能退回 LOD0），表现为"没有简化"。

---

## 数学原理

### 1. 16-bit 顶点量化

**编码：**
$$
q = \text{round}\left( \frac{p - \text{aabbMin}}{\text{aabbMax} - \text{aabbMin}} \times 65535 \right)
$$

**解码（Shader 中）：**
$$
p = \text{aabbMin} + \frac{q}{65535} \times (\text{aabbMax} - \text{aabbMin})
$$

### 2. 法线锥计算

给定 Cluster 中所有三角形法线 $\{n_1, n_2, ..., n_k\}$：

1. **锥轴：** 法线的平均方向（归一化）
$$
\text{axis} = \text{normalize}\left( \sum_{i=1}^{k} n_i \right)
$$

2. **半角余弦：** 最大偏离角的余弦
$$
\cos(\text{halfAngle}) = \min_{i=1}^{k} (n_i \cdot \text{axis})
$$

### 3. 法线锥剔除条件

设相机到 Cluster 中心的方向为 $v$，法线锥轴为 $a$，半角为 $\theta$。

当且仅当 $v \cdot a < -\sin(\theta)$ 时，Cluster 的所有三角形都背向相机，可以被剔除。

---

## 配置参数

### Nanite.h 配置

```cpp
namespace NaniteConfig {
    constexpr uint32_t CLUSTER_MAX_TRIANGLES = 128;  // 每个 Cluster 最大三角形数
    constexpr uint32_t CLUSTER_TARGET_TRIANGLES = 64; // 目标三角形数
    constexpr uint32_t MAX_CLUSTERS_PER_MESH = 65536; // 单个网格最大 Cluster 数
    constexpr uint32_t POSITION_BITS = 16;           // 位置量化精度
    constexpr uint32_t NORMAL_BITS = 16;             // 法线量化精度
}
```

---

## 实现进度

| 阶段 | 功能 | 状态 | 说明 |
|------|------|------|------|
| Phase 1 | Cluster 数据结构 | ✅ 完成 | GPU 对齐的 96 字节结构 |
| Phase 1 | 三角形邻接图 | ✅ 完成 | 边哈希表实现 |
| Phase 1 | 贪心聚类算法 | ✅ 完成 | 优先队列 + 空间/法线权重 |
| Phase 1 | 顶点量化打包 | ✅ 完成 | 16-bit 量化 |
| Phase 1 | 法线锥计算 | ✅ 完成 | 用于整 Cluster 背面剔除 |
| Phase 1 | NaniteManager | ✅ 完成 | GPU 缓冲管理 |
| Phase 1 | 渲染器集成 | ✅ 完成 | RHI 双后端 (Vulkan/DX12) |
| Phase 2 | LOD 生成 | ✅ 完成 | `MeshSimplifier` QEM 边坍缩 + 边界锁定 |
| Phase 2 | Cluster Group 层级 | ✅ 完成 | 每 4 个相邻 Cluster 一组,父索引回填 |
| Phase 3 | GPU 屏幕空间 LOD 选择 | ✅ 完成 | DAG 规则 `selfError <= T < parentError` |
| Phase 3 | Cluster BVH | 🔲 待实现 | 层级剔除加速 (当前全量 dispatch) |
| Phase 4 | Software Rasterizer | 🔲 待实现 | 小三角形软光栅 |
| Phase 4 | Visibility Buffer | 🔲 待实现 | 延迟材质着色 |

> 当前可见列表经双缓冲 readback 回 CPU 后逐 Cluster `drawIndexed`（Nanite 仅用于
> Cluster 可视化路径）。完全 GPU-Driven 间接绘制（command signature / indirect draw）
> 仍是 backlog。

---

## 调试与验证

### 快捷键

| 按键 | 功能 |
|------|------|
| `5` | 切换 Forward / Deferred 场景 |
| `7` | 切换 Nanite 渲染开/关 |
| `8` | 执行测试网格的 Cluster 划分 + LOD 生成 + 上传 |
| `9` | 开/关 Cluster 可视化（同时启用 GPU culling） |
| `0` | 循环调试模式：ClusterColor → Normal → LOD → HashColor |
| `B` | 循环 Force LOD：OFF → 0 → 1 → … → 7（诊断用，显示被自动选择跳过的层级） |
| `X` | 切换法线锥背面剔除 |
| `Z` | 切换视锥剔除 |

> **Force LOD 原理**：强制某级时自动关闭 GPU LOD 选择（`w=0`），让所有层级先通过
> 视锥/法线锥剔除，再由 CPU 按 `lodLevel` 过滤；层级不存在的 mesh 用根节点兜底。

### 输出验证

```
[Nanite] Processing mesh: ../../assets/UFO/UFO_Empty.obj (27844 triangles)
[MeshClusterizer] Generated 218 clusters from 27844 triangles
[MeshClusterizer] LOD 1: 55 clusters
[MeshClusterizer] LOD 2: 14 clusters
[MeshClusterizer] LOD 3: 4 clusters
[Nanite] GPU upload complete
Clustering done: 3 meshes, 376 clusters
...
[LOD] Drawn:71/376 | GPU LOD Selection | Visible:71 | [L0:18 L1:48 L2:5 ]
```

`[LOD]` 行的分布随相机距离变化（近处 L0/L1 为主，远处 L2/L3 为主），
即 GPU 端 DAG LOD 选择生效。

### RenderDoc 检查

1. 检查 `ClusterBuffer` SSBO 的内容和对齐
2. 验证 `VisibleBuffer` 的输出计数
3. 确认 Compute Dispatch 的工作组数量正确

---

## 性能特点

| 指标 | 传统渲染 | Nanite Cluster |
|------|---------|----------------|
| 剔除粒度 | 每物体 | 每 128 三角形 |
| Draw Call | 物体数量级 | 1 次间接绘制 |
| 顶点内存 | 全精度 32-bit | 量化 16-bit |
| 背面剔除 | 逐三角形 GPU | 整 Cluster GPU |

---

## 参考资料

- [A Deep Dive into Nanite Virtualized Geometry (SIGGRAPH 2021)](https://advances.realtimerendering.com/s2021/)
- [Nanite | Inside Unreal](https://www.youtube.com/watch?v=eviSykqSUUw)
- [Mesh Shaders and Amplification Shaders (NVIDIA)](https://developer.nvidia.com/blog/introduction-turing-mesh-shaders/)
- [UE5 Nanite 源码分析](https://zhuanlan.zhihu.com/p/382687738)
