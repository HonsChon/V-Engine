# V Engine - AI Context Document

> 此文档供 AI 编程助手了解项目当前状态，便于在新对话中快速上手。
> 最后更新: 2026/03/10  版本: v0.11.2

---

## 项目概述

**V Engine** 是一个基于 Vulkan 的现代游戏引擎学习项目，专注于图形渲染技术和 ECS 架构。

### 技术栈
- 图形 API: Vulkan 1.3
- 语言: C++17
- 窗口库: GLFW
- 数学库: GLM
- ECS: EnTT
- UI: ImGui
- 构建系统: CMake


### 项目路径
- 根目录: f:\图形学习\PBR\PBR
- 源代码: src/
- 着色器: shaders/ (GLSL -> SPIR-V)
- 资源: assets/
- 构建输出: build/bin/

---

## 代码架构

### 核心目录结构

```
src/
├── core/           # Vulkan 底层封装
│   ├── VulkanDevice.*      # 设备管理
│   ├── VulkanSwapChain.*   # 交换链
│   ├── VulkanBuffer.*      # GPU 缓冲区
│   ├── VulkanTexture.*     # 纹理管理
│   ├── ComputePipeline.*   # 计算管线封装
│   └── ComputePassBase.*   # 计算通道基类
│
├── renderer/       # 主渲染器
│   ├── VulkanRenderer.*    # 协调所有 Pass
│   └── GPUDrivenRenderer.* # GPU驱动渲染器
│
├── passes/         # 模块化渲染通道
│   ├── RenderPassBase.h    # Pass 基类
│   ├── GBufferPass.*       # G-Buffer 阶段
│   ├── SSRPass.*           # 屏幕空间反射
│   ├── WaterPass.*         # 水面渲染
│   ├── FrustumCullingPass.*# GPU视锥体剔除
│   └── NaniteDebugPass.*   # Nanite Cluster 可视化 (NEW)
│
├── nanite/         # Nanite 虚拟几何系统 (NEW)
│   ├── MeshClusterizer.*   # 网格聚类算法
│   └── NaniteCommon.h      # 数据结构定义
│
├── scene/          # ECS 场景管理
│   ├── Scene.*             # 场景容器
│   └── Components.h        # ECS 组件
│
└── ui/             # 编辑器 UI
    └── panels/             # UI 面板
```

### 渲染管线 (延迟渲染模式)
```
GBufferPass -> SSRPass -> LightingPass -> WaterPass -> Swapchain
                                              ↓
                              [Key 9] NaniteDebugPass (覆盖渲染)
```

---

## 已完成功能 (v0.11.0)

### 渲染系统
- [x] Vulkan 基础设施
- [x] 双渲染管线 (前向 + 延迟)
- [x] G-Buffer MRT (Position, Normal, Albedo, Depth)
- [x] PBR 材质 (Cook-Torrance BRDF)
- [x] 屏幕空间反射 (SSR) - 线性深度版
- [x] 水面渲染 - 含智能遮挡
- [x] GPU-Driven Rendering (Phase 1) - 视锥体剔除

### Nanite 虚拟几何系统 (v0.11.0 NEW)
- [x] **Phase 1: Mesh Clustering** - 贪婪聚类算法
- [x] **Cluster 可视化调试** - Golden Ratio 着色
- [x] **全场景 Cluster 可视化** - 支持多网格同时渲染 (v0.11.1)
- [ ] Phase 2: Mesh Simplification (LOD 生成)
- [ ] Phase 2: Hierarchy Construction (DAG 构建)
- [ ] Phase 3: Runtime LOD Selection

### GPU-Driven 渲染系统 (v0.10.0)
- [x] Compute Pipeline 基础设施
- [x] GPU 视锥体剔除 (Frustum Culling)
- [x] Indirect Draw Buffer 生成
- [x] ECS 到 GPU 数据同步
- [x] 运行时切换 (Key 6)

### 场景系统
- [x] ECS 架构 (EnTT)
- [x] 射线拾取 (AABB)
- [x] FPS 相机控制
- [x] SelectionManager 单例 (UI状态管理)

### 编辑器
- [x] ImGui 集成
- [x] 调试/场景/属性面板
- [x] Scene Hierarchy 选择同步


---

## 核心技术实现细节

### 1. Nanite Mesh Clustering (METIS 风格)

文件: src/nanite/MeshClusterizer.cpp, src/passes/NaniteDebugPass.cpp

#### METIS 风格多级图分区算法
```cpp
// 目标: 将网格分割为 ~128 三角形的 Cluster
// 算法: 与 UE5 Nanite 相同的多级图分区方法

// 核心流程:
// 阶段 1 - 粗化 (Coarsening):
//   1. 构建三角形邻接图 (边哈希表)
//   2. 重边缘匹配 (Heavy Edge Matching) - 合并高权重边的节点
//   3. 重复粗化直到节点数足够小
//
// 阶段 2 - 初始分区 (Initial Partitioning):
//   1. 选择 k 个分散的种子点 (最远点采样)
//   2. BFS 交替扩展 (优先高权重边)
//   3. 平衡约束控制分区大小
//
// 阶段 3 - 细化 (Refinement):
//   1. 从粗到细逐层投影分区结果
//   2. KL/FM 风格局部优化边界节点
//   3. 计算移动增益并优化切边

// 边权重计算 (决定合并优先级):
float computeEdgeWeight(tri1, tri2) {
    weight *= (1.0 + sharedEdgeLength);     // 共享边长度
    weight *= normalSimilarity;              // 法线相似度
    weight *= areaSimilarity;                // 面积因子
    return weight;
}
```

#### 可视化着色 (Golden Ratio)
```glsl
// shaders/nanite/cluster_debug.frag
// 使用黄金分割比生成高区分度颜色
vec3 clusterColor(int index) {
    float hue = fract(float(index) * 0.618033988749895);  // φ
    return hsv2rgb(vec3(hue, 0.7, 0.9));
}
```

#### 关键数据结构
```cpp
// ClusterizedMesh - 聚类后的网格
struct ClusterizedMesh {
    std::vector<Cluster> clusters;
    uint32_t originalVertexCount;
    uint32_t originalIndexCount;
    uint32_t totalClusterTriangles;
};

// Push Constants (144 bytes)
struct ClusterDebugPushConstants {
    glm::mat4 model;         // 64 bytes
    glm::mat4 normalMatrix;  // 64 bytes
    int clusterIndex;        // 4 bytes
    int debugMode;           // 4 bytes
    float padding[2];        // 8 bytes
};
```

#### 调试模式
| Key | 功能 |
|-----|------|
| **9** | 切换 Cluster 可视化开关 |
| **0** | 循环调试模式 (Color/Normals/LOD/Hash) |

#### 调试模式详解
- **Mode 0 - Color**: 每个 Cluster 独立颜色 (Golden Ratio HSV)
- **Mode 1 - Normals**: 显示法线方向
- **Mode 2 - LOD**: 预留 LOD 层级显示
- **Mode 3 - Hash**: 基于位置的哈希着色


### 2. 屏幕空间反射 (SSR)

文件: shaders/ssr.frag, src/passes/SSRPass.cpp

#### 核心算法: 透视正确的屏幕空间射线步进

```glsl
// 步进在 NDC 空间进行（透视正确）
vec3 startScreen = worldToScreen(rayOrigin);   // (UV.x, UV.y, NDC_depth)
vec3 endScreen = worldToScreen(rayOrigin + rayDir * maxDistance);
vec3 stepScreen = (endScreen - startScreen) / numSteps;

// 深度比较在线性空间（世界单位阈值）
float linearizeDepth(float depth) {
    return near * far / (far - depth * (far - near));
}
```


#### 关键参数
| 参数 | 当前值 | 说明 |
|------|--------|------|
| maxDistance | 50.0 | 最大射线距离(世界单位) |
| maxSteps | 64 | 步进次数 |
| thickness | 0.01 | 厚度阈值(世界单位,米) |
| nearPlane | 0.1 | 近平面 |
| farPlane | 100.0 | 远平面 |

#### 设计要点
1. NDC 步进: 保持透视正确性
2. 线性深度比较: 厚度阈值为世界单位
3. 二分搜索细化: 8 次迭代


### 3. 水面渲染

文件: shaders/water.frag, src/passes/WaterPass.cpp

#### 智能深度遮挡逻辑

问题: Final Pass 会清除深度缓冲，硬件深度测试失效。

解决方案: 片段着色器内的手动深度测试：

```glsl
// 结合世界高度 + 线性深度判断遮挡
float heightDiff = sceneWorldPos.y - waterHeight;  // 正=物体在水上
float depthDiff = sceneLinearDepth - waterLinearDepth;  // 负=物体更近

// 情况1: 物体在水面上方且更近 -> 遮挡水面
if (heightDiff > 0.01 && depthDiff < 0.0) {
    edgeSoftness = smoothstep(-0.05, 0.0, depthDiff);
}
// 情况2: 物体在水面下方 -> 水面始终可见
else if (heightDiff < -0.01) {
    edgeSoftness = 1.0;
}
```


### 4. GPU-Driven Rendering

文件: src/passes/FrustumCullingPass.cpp, shaders/culling/frustum_culling.comp

#### 架构概述
```
[CPU阶段]                    [GPU阶段]
EnTT Registry                Frustum Culling Pass
    ↓                              ↓
GPUInstanceData[]  ───────>  Compute Shader
FrustumPlanes[6]   ───────>      ↓
                            Indirect Draw Buffer
                                   ↓
                            vkCmdDrawIndexedIndirect()
```

#### 视锥体剔除算法 (Sphere-Plane Test)
```glsl
bool isInsideFrustum(vec3 center, float radius) {
    for(int i = 0; i < 6; i++) {
        float distance = dot(frustumPlanes[i].xyz, center) + frustumPlanes[i].w;
        if(distance < -radius) return false;  // 完全在平面外
    }
    return true;
}
```

#### 数据结构
```cpp
// GPU 实例数据 (140 bytes aligned)
struct GPUInstanceData {
    glm::mat4 model;          // 64 bytes - 模型矩阵
    glm::mat4 normalMatrix;   // 64 bytes - 法线矩阵
    glm::vec4 boundingSphere; // 16 bytes - xyz=中心, w=半径
};
```

---

## 已知问题和调试要点

### Nanite Cluster 可视化相关 (NEW)
1. **只看到球体内部**: 原因是背面剔除 + 可能的三角形缠绕顺序问题
   - 解决: `VK_CULL_MODE_NONE` + 双面光照 (法线翻转)
2. **按键无响应**: 检查 `hasClusterData()` 门控逻辑
3. **颜色不明显**: 调整 HSV 饱和度/明度参数

### SSR 相关
1. 反射抖动/噪声: thickness 太大(>0.5)产生噪声，太小(<0.001)可能漏检
2. 反射不对称: 确保使用 NDC 空间步进
3. 远距离精度: 必须使用线性深度比较

### 水面相关
1. 水面遮挡整个场景: 检查深度遮挡逻辑
2. 水下物体浮出: 添加 heightDiff 检查

### 调试宏（着色器末尾）
```glsl
// outColor = vec4(reflectDir * 0.5 + 0.5, 1.0);  // 反射方向
// outColor = vec4(vec3(reflectionStrength), 1.0); // 命中强度
// outColor = vec4(vec3(depthDiff * 0.1), 1.0);    // 深度差
```


---

## 重要文件快速索引

| 文件路径 | 用途 |
|---------|------|
| **Nanite 系统 (NEW)** | |
| src/nanite/MeshClusterizer.cpp | 网格聚类算法实现 |
| src/nanite/NaniteCommon.h | Cluster/ClusterizedMesh 数据结构 |
| src/passes/NaniteDebugPass.cpp | Cluster 可视化渲染通道 |
| shaders/nanite/cluster_debug.vert | Cluster 调试顶点着色器 |
| shaders/nanite/cluster_debug.frag | Cluster 调试片段着色器 (Golden Ratio) |
| **渲染系统** | |
| shaders/water.frag | 水面着色器（含 SSR + 遮挡逻辑）|
| shaders/ssr.frag | 全屏 SSR 着色器 |
| shaders/gbuffer.frag | G-Buffer 输出 |
| shaders/deferred_lighting.frag | 延迟光照 PBR |
| shaders/culling/frustum_culling.comp | GPU剔除着色器 |
| src/passes/SSRPass.cpp | SSR 通道参数设置 |
| src/passes/WaterPass.cpp | 水面通道 |
| src/passes/FrustumCullingPass.cpp | GPU视锥体剔除 |
| src/renderer/VulkanRenderer.cpp | 主渲染器 |
| src/renderer/GPUDrivenRenderer.cpp | GPU驱动渲染协调器 |
| src/core/ComputePipeline.cpp | 计算管线封装 |
| **场景/ECS** | |
| src/scene/Components.h | ECS 组件定义 |
| src/ui/SelectionManager.h | UI选择状态管理 |
| **文档** | |
| docs/gpu-driven-rendering/README.md | GPU-Driven架构文档 |
| README.md | 项目完整文档 |


---

## 快捷键速查表

| 按键 | 功能 |
|------|------|
| **W/A/S/D** | 相机移动 |
| **鼠标右键** | 相机旋转 |
| **6** | 切换 GPU-Driven 渲染 |
| **9** | 切换 Nanite Cluster 可视化 (NEW) |
| **0** | 循环 Cluster 调试模式 (NEW) |

---

## 待开发功能 (路线图)

### Nanite 虚拟几何系统 (当前重点)
- [x] Phase 1: Mesh Clustering ✓
- [ ] **Phase 2: Mesh Simplification** (下一步)
  - [ ] 边折叠算法 (QEM - Quadric Error Metrics)
  - [ ] LOD 层级生成
- [ ] **Phase 2: Hierarchy Construction**
  - [ ] DAG 构建 (Cluster Group)
  - [ ] 父子依赖关系
- [ ] Phase 3: Runtime LOD Selection
- [ ] Phase 4: Software Micro-Rasterizer
- [ ] Phase 5: Streaming + Virtual Geometry

### v1.0.0 计划
- [ ] 多光源支持
- [ ] 阴影系统 (Shadow Mapping / CSM)
- [ ] SSAO
- [ ] 后处理 (Bloom, Tone Mapping, FXAA)
- [ ] 天空盒 + IBL

### v2.0.0 计划
- [ ] 骨骼动画
- [ ] 物理系统
- [ ] 粒子系统
- [ ] 地形系统


---

## 常用开发命令

```bash
# 编译项目
cd build
cmake --build . --config Debug

# 编译着色器
cd shaders
glslc ssr.frag -o ssr_frag.spv
glslc water.frag -o water_frag.spv
glslc nanite/cluster_debug.vert -o nanite/cluster_debug_vert.spv
glslc nanite/cluster_debug.frag -o nanite/cluster_debug_frag.spv

# 运行
cd build/bin
./VulkanPBR.exe
```


---

## 开发历史笔记

### 2026/03/09 - Nanite Phase 1: Mesh Clustering (v0.11.0)
1. 实现 MeshClusterizer 贪婪聚类算法
2. 实现 NaniteDebugPass Cluster 可视化
3. 创建 cluster_debug.vert/frag 着色器 (Golden Ratio HSV)
4. 集成到 VulkanRenderer，Key 9/0 切换调试
5. 修复背面剔除问题 (VK_CULL_MODE_NONE + 双面光照)

#### Bug修复记录 (v0.11.0)
- **C3668**: `cleanup()` override 签名不匹配 → 移除 override
- **C2039**: `getMatrix` 不存在 → 改为 `getTransform()`
- **C2660**: `getProjectionMatrix` 参数不匹配 → 添加 aspect, zoom 参数
- **静默失败**: `hasClusterData()` 门控阻止首次构建 → 调整逻辑
- **只看到内部**: 背面剔除 → `VK_CULL_MODE_NONE` + 法线翻转

### 2026/03/08 - GPU-Driven Rendering Phase 1 (v0.10.0)
1. 实现 ComputePipeline 计算管线封装
2. 实现 ComputePassBase 计算通道基类（含内存屏障）
3. 实现 FrustumCullingPass GPU视锥体剔除
4. 实现 GPUDrivenRenderer 协调器
5. 创建 frustum_culling.comp 剔除着色器
6. 集成到 VulkanRenderer，Key 6 切换
7. 修复 SelectionManager UI选择同步问题

### 2024/02/24 - SSR 线性深度重构
1. SSR 从非线性深度迁移到线性深度
2. 修复反射不对称问题（改用 NDC 步进 + 线性比较）
3. 修复水面遮挡全场景 bug（手动深度测试）
4. 修复水下物体浮出问题（添加世界高度检查）
5. 优化 thickness 参数: 0.5 -> 0.1 -> 0.01

---

## 当前工作状态

### 上次会话结束点 (2026/09/12 v0.12.1)
- **完成**: 修复 cone culling 公式(`-cos` → `-sin`),消除宽锥 cluster 的过度剔除
- **完成**: cluster 变换修正(包围球半径乘缩放、法线锥轴用逆转置矩阵)
- **完成**: `MeshSimplifier` 分离"几何误差"与"惩罚代价",lodError 不再被 +100/+50 污染
- **完成**: Force LOD 诊断(键 `B` 循环 -1..7;强制时关闭 GPU LOD 选择后 CPU 过滤)
- **完成**: `X`/`Z` 切换 cone/frustum 剔除;F1 面板滑条
- **状态**: Force LOD 序列 DX12/VK 一致(LOD0→250 簇、L1→70、L2→20、L3→10);
  DX12 Debug validation 0 error(仅存量 clear-value 性能 warning)

### 已修复: 简化 mesh "看起来没简化" / 块状空洞 (2026/09/12)

**现象**: 键 8+9 后 cluster 可视化出现一块一块的洞,且感觉还是 LOD0 的 mesh。

**根因(两个)**:
1. `cluster_culling.comp` 的 cone 剔除条件写成了 `dot(viewDir, axis) < -cos θ`,
   正确应为 `-sin θ`。对半角 >45° 的宽锥 cluster(主要是 LOD1+ 合并簇)会误判为背面,
   整块剔除;而子簇又因 `parentError <= T` 被 LOD 规则跳过 → 块状空洞。
2. `MeshSimplifier` 用含惩罚的折叠代价(边界 +100、锁定顶点 +50)更新 `m_maxError`,
   再作为 `geometricError` 传给 lodError → 误差被放大到 50~100(正常 1e-5 量级),
   父级再累加 `maxChildError` → 这些区域永远选不中 L1+,只能退回 LOD0。

**修复**:
- `coneCull()` 改用 `-sqrt(max(0, 1-cos²))`;cone 轴按 mesh 逆转置矩阵变换;
  包围球半径乘最大轴缩放。
- `EdgeCollapse` 增加 `geometricError`(纯 QEM,不含惩罚),优先队列仍用含惩罚的
  `error` 排序,`m_maxError`/`m_totalError` 改用 `geometricError`。
- 新增 Force LOD(`B` 键 + F1 滑条):强制某级时 `w=0` 关闭 GPU LOD 选择,
  所有层级先过剔除,再由 CPU 按 `lodLevel` 过滤(层级不存在的 mesh 用根兜底)。
- `X`/`Z` 键切换 cone/frustum 剔除;`performCulling` 不再硬编码这两个开关。

**验证**:
- 误差恢复:UFO L1 ≈ 5e-5、sphere L1 ≈ 4.3e-6(不再出现 50/100)。
- Force LOD 统计:DX12 `LOD0:250 / LOD1:70 / LOD2:20 / LOD3:10`,VK 同;
  相邻层级截图 diff ≈19%(简化可见)。
- DX12 Debug:键 8→9→0→B×9→X→Z soak,validation 0 error。
- Vulkan Release:stderr 0,Force LOD 序列与 DX12 一致。

**已知残留**:
- QEM 折叠顺序仍受 `unordered_map` 迭代顺序影响 → 每次运行简化结果/误差略有差异
  (量级已正常,不再影响可用性)。彻底确定性化留作后续。
- `MeshSimplifier` 的边界惩罚仍可能让少数 collapse 代价偏高(排序层面),属设计如此。

### ✅ 已解决: GPU Culling 颜色颤抖 (2026/09/11)

**原问题**: GPU readback 的可见 cluster 列表不稳定,可视化每帧跳变。

**根因**:
- `ClusterCullingPass::record()` 用 `imageIndex` 选 readback slot,而
  `Engine::readbackCullingResults()` 用 `frameIndex` —— 交换链图像数(3)与帧槽数(2)
  不同,CPU 可能读到尚未完成/未写入的 slot。
- 另外 GPU 端没有真正的 LOD 选择,CPU 用硬编码距离阈值"整 mesh 切 LOD"。

**修复**:
1. readback slot 统一用 `frameIndex`(`SceneRenderer::prepareNaniteCulling` /
   `recordNaniteDebugCommands`);`ClusterCullingPass` 增加 per-slot `m_readbackValid`,
   未写入的 slot 不回读。
2. `cluster_culling.comp` 实现真正的 Nanite DAG LOD 选择:
   `selfScreenError <= threshold < parentScreenError`,并支持每 mesh 世界矩阵
   (`GPUClusterData.meshIndex` + TransformBuffer)。
3. `NaniteDebugPass::recordCommandsWithLOD` 删除 CPU 距离阈值逻辑,直接绘制 GPU
   回读的可见列表;回读未就绪时降级只画 LOD0。
4. 键 `0` 循环调试模式(ClusterColor/Normal/LOD/HashColor),LOD 模式按层级着色;
   F1 Debug Panel 可实时调 `screenSpaceErrorThreshold` / `lodErrorScale`。

**验证**:
- DX12/Vulkan Release:近处 `L0:18 L1:48 L2:5`,后退后 `L0:6 L1:4 L2:12 L3:1`。
- 同机位截图跨运行/跨后端一致(UI 关闭后逐像素对比)。
- DX12 Debug validation 0 error(见 docs/DX12-Port-Plan.md)。

**已知残留**:
- ~~`MeshSimplifier` 的 QEM 误差被 seam/boundary 惩罚放大到 ~50~~ → 已于 2026/09/12
  分离 `geometricError` 修复(见上节)。

### 下一步建议
1. 让 `MeshSimplifier` 完全确定(unordered_map 迭代顺序 → 排序后的确定性遍历)
2. Cluster BVH / 层级剔除,避免每帧全量 dispatch
3. 完全 GPU-Driven 间接绘制(command signature + indirect draw),去掉 readback

### 当前渲染模式
```
NaniteDebugPass: GPU DAG LOD 选择
- GPU 一次 dispatch 完成 世界变换 + LOD 选择 + 视锥/法线锥剔除
- 双缓冲 readback 后 CPU 逐 cluster 绘制(2 帧延迟)
- 回读未就绪时降级为只画 LOD0
- 诊断: B=Force LOD 循环, X=Cone 开关, Z=Frustum 开关, 0=调试模式循环
```

### 关键代码位置
```cpp
// shaders/nanite/cluster_culling.comp  - DAG 规则 + 每 mesh 变换 + abs(proj) + cone -sin
// NaniteManager::performCulling()      - y/z/w = frustum/cone/LODSelection
// NaniteManager::setMeshTransforms()   - 每 mesh 世界矩阵上传
// MeshSimplifier.cpp                   - geometricError 与惩罚分离
// NaniteDebugPass::recordCommandsWithLOD() - GPU 可见列表 + Force LOD 过滤
// SceneRenderer::prepareNaniteCulling() - frameIndex readback slot + UI 调参
```


---

*此文档由 AI 助手生成，用于帮助后续 AI 对话快速理解项目上下文。*
*新对话时，请 AI 先阅读此文件: .ai/PROJECT_STATUS.md*
