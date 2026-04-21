# GPU-Driven Rendering / Nanite 实现文档

## 概述

本模块实现了类似 UE5 Nanite 的 GPU 驱动渲染系统。核心思想是将传统的 CPU 端剔除和绘制决策转移到 GPU 上，通过 Compute Shader 实现高效的场景剔除和间接绘制。

## 架构图

```
┌─────────────────────────────────────────────────────────────┐
│                    GPUDrivenRenderer                        │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                 Culling Pipeline                     │   │
│  │  ┌───────────────┐  ┌───────────────┐  ┌──────────┐ │   │
│  │  │   Frustum     │  │   Occlusion   │  │   LOD    │ │   │
│  │  │   Culling     │→ │   Culling     │→ │ Selection│ │   │
│  │  │   (Phase 1)   │  │   (Phase 2)   │  │ (Phase 3)│ │   │
│  │  └───────────────┘  └───────────────┘  └──────────┘ │   │
│  └─────────────────────────────────────────────────────┘   │
│                              ↓                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              Indirect Draw Buffer                    │   │
│  │   VkDrawIndexedIndirectCommand[]                     │   │
│  └─────────────────────────────────────────────────────┘   │
│                              ↓                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │         vkCmdDrawIndexedIndirect()                   │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## 核心组件

### 1. ComputePipeline (`src/core/ComputePipeline.h`)

通用的 Vulkan Compute Pipeline 封装，支持：
- SSBO (Shader Storage Buffer Object) 绑定
- Push Constants
- 直接和间接调度

```cpp
ComputePipeline::Config config;
config.shaderPath = "shaders/culling/frustum_culling.comp.spv";
config.bindings = {
    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
    {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
    // ...
};
auto pipeline = std::make_unique<ComputePipeline>(device, config);
```

### 2. ComputePassBase (`src/passes/ComputePassBase.h`)

所有 Compute Pass 的基类，提供：
- Pipeline 管理
- Descriptor Set 管理
- Buffer Barrier 辅助函数

### 3. FrustumCullingPass (`src/passes/FrustumCullingPass.h`)

GPU 视锥剔除实现：

**输入:**
- `InstanceBuffer`: 所有物体的变换矩阵和包围盒
- `CullingUniforms`: 相机矩阵和视锥体平面

**输出:**
- `VisibleIndicesBuffer`: 通过剔除测试的实例索引
- `IndirectDrawBuffer`: 可直接用于 `vkCmdDrawIndexedIndirect` 的命令

### 4. GPUDrivenRenderer (`src/passes/GPUDrivenRenderer.h`)

整合所有剔除 Pass 的高层管理器。

## 数据流

```
CPU:                          GPU:
┌─────────────┐              ┌─────────────────────────────────┐
│ Scene Data  │   Upload     │                                 │
│ (Transforms │ ─────────→   │  Instance Buffer (SSBO)         │
│  BBoxes)    │              │                                 │
└─────────────┘              └─────────────────────────────────┘
                                         │
                                         ↓ Compute Shader
┌─────────────┐              ┌─────────────────────────────────┐
│ Camera      │   Upload     │                                 │
│ (VP Matrix) │ ─────────→   │  Uniform Buffer                 │
└─────────────┘              │                                 │
                             └─────────────────────────────────┘
                                         │
                                         ↓ Frustum Cull
                             ┌─────────────────────────────────┐
                             │  Visible Indices (SSBO)         │
                             │  [0, 3, 7, 12, ...]            │
                             └─────────────────────────────────┘
                                         │
                                         ↓ Generate Commands
                             ┌─────────────────────────────────┐
                             │  Indirect Draw Buffer           │
                             │  [{indexCount, instanceCount,   │
                             │    firstIndex, ...}, ...]       │
                             └─────────────────────────────────┘
                                         │
                                         ↓ vkCmdDrawIndexedIndirect
                             ┌─────────────────────────────────┐
                             │  GPU Rendering                  │
                             └─────────────────────────────────┘
```

## Shader 说明

### frustum_culling.comp

```glsl
layout(local_size_x = 256) in;

// 视锥剔除核心算法
bool frustumCullSphere(vec3 center, float radius) {
    for (int i = 0; i < 6; ++i) {
        float distance = signedDistanceToPlane(frustumPlanes[i], center);
        if (distance < -radius) {
            return false;  // 完全在视锥外
        }
    }
    return true;  // 在视锥内或相交
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= instanceCount) return;
    
    InstanceData inst = instances[idx];
    
    // 变换包围球到世界空间
    vec3 worldCenter = (inst.modelMatrix * vec4(inst.boundingSphere.xyz, 1.0)).xyz;
    float worldRadius = inst.boundingSphere.w * maxScale(inst.modelMatrix);
    
    if (frustumCullSphere(worldCenter, worldRadius)) {
        // 通过测试，写入输出
        uint outputIdx = atomicAdd(visibleCount, 1);
        visibleIndices[outputIdx] = idx;
        
        // 生成间接绘制命令
        drawCommands[outputIdx] = ...;
    }
}
```

## 使用方法

### 初始化

```cpp
// 创建 GPU-Driven Renderer
GPUDrivenRenderer::Config config;
config.enableFrustumCulling = true;
config.maxInstances = 100000;

gpuDrivenRenderer = std::make_unique<GPUDrivenRenderer>(device, config);
gpuDrivenRenderer->init();
```

### 每帧更新

```cpp
// 1. 准备实例数据
std::vector<GPUInstanceData> instances;
for (auto& entity : scene.entities) {
    GPUInstanceData data;
    data.modelMatrix = entity.transform;
    data.boundingSphere = entity.mesh.boundingSphere;
    data.meshIndex = entity.meshId;
    data.flags = 1;  // 启用
    instances.push_back(data);
}

// 2. 更新 GPU Renderer
gpuDrivenRenderer->prepare(instances, camera.viewMatrix, camera.projMatrix, camera.position);

// 3. 在 Command Buffer 中执行剔除
gpuDrivenRenderer->executeCulling(commandBuffer);

// 4. 使用间接绘制
vkCmdDrawIndexedIndirect(commandBuffer, 
                          gpuDrivenRenderer->getIndirectDrawBuffer(),
                          0,
                          maxDrawCount,
                          sizeof(VkDrawIndexedIndirectCommand));
```

## 实现进度

| 阶段 | 功能 | 状态 |
|------|------|------|
| Phase 1 | ComputePipeline 基础设施 | ✅ 完成 |
| Phase 1 | ComputePassBase 抽象 | ✅ 完成 |
| Phase 1 | FrustumCullingPass | ✅ 完成 |
| Phase 1 | GPUDrivenRenderer 集成 | ✅ 完成 |
| Phase 2 | HZB (Hierarchical Z-Buffer) | 🔲 待实现 |
| Phase 2 | OcclusionCullingPass | 🔲 待实现 |
| Phase 3 | Mesh Clustering | ✅ 完成 | → [Nanite 文档](../nanite/README.md) |
| Phase 3 | LOD Selection | 🔲 待实现 |
| Phase 4 | Software Rasterization | 🔲 待实现 |

## 性能优化建议

1. **批处理**: 尽量减少 Compute Dispatch 次数
2. **内存布局**: 确保 SSBO 数据对齐到 16 字节
3. **工作组大小**: 256 是大多数 GPU 的最佳值
4. **异步计算**: 使用独立的 Compute Queue 与 Graphics Queue 并行

## 调试技巧

1. 使用 RenderDoc 检查 Compute Shader 的输入/输出
2. 将 Counter Buffer 回读到 CPU 验证剔除数量
3. 使用 Validation Layers 检查同步问题

## 参考资料

- [GPU-Driven Rendering Pipelines (SIGGRAPH 2015)](http://advances.realtimerendering.com/s2015/)
- [A Deep Dive into Nanite (SIGGRAPH 2021)](https://advances.realtimerendering.com/s2021/)
- [Vulkan Guide: GPU Driven Rendering](https://vkguide.dev/docs/gpudriven/)
