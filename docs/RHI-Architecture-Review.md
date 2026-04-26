# RHI 层架构问题分析与改进建议

> **日期**: 2026/04/26  
> **范围**: `src/RHI/` 目录及各 Render Pass 中的 Vulkan 资源管理代码  
> **状态**: 问题分析 + 改进方案草案

---

## 1. 概述

当前项目的 `src/RHI/` 目录旨在提供 Vulkan 底层抽象层（RHI），但在实际使用中，大部分 Render Pass 绕过了 RHI 封装，直接调用 Vulkan 原生 API 完成 pipeline 创建、descriptor 管理、image/buffer 操作等工作。这导致了大量重复代码散落在各个 pass 中，RHI 层未能发挥应有的抽象和复用作用。

---

## 2. 当前 RHI 层文件清单及实际复用情况

| 文件 | 职责 | 被 Pass 实际复用程度 | 评价 |
|------|------|---------------------|------|
| `VulkanDevice.h/cpp` | Vulkan 实例、物理/逻辑设备、队列、命令池 | ✅ 广泛使用 | **有用**，核心设备管理 |
| `VulkanSwapChain.h/cpp` | 交换链、Render Pass、帧缓冲 | ✅ 广泛使用 | **有用**，交换链管理 |
| `FrameResources.h/cpp` | 帧同步原语（Fence/Semaphore）、命令缓冲 | ✅ 广泛使用 | **有用**，帧同步管理 |
| `VulkanBuffer.h/cpp` | Buffer 创建、映射、拷贝 | ⚠️ 部分使用 | Pass 中经常绕过此类，直接调 `device->createBuffer()` |
| `VulkanTexture.h/cpp` | 纹理加载（从文件）、Image/ImageView/Sampler | ⚠️ 仅用于资源加载 | Pass 内部创建 RT/Attachment 时自行裸调 Vulkan API |
| `VulkanPipeline.h/cpp` | 硬编码的 PBR 前向管线 + Descriptor Layout | ❌ 几乎无人使用 | **最鸡肋**，详见下文 |
| `ComputePipeline.h/cpp` | Compute Pipeline 封装 | ⚠️ 仅 `ClusterCullingPass` 使用 | 有一定封装价值，但使用面极窄 |

---

## 3. 核心问题

### 3.1 `VulkanPipeline` 定位混乱

`VulkanPipeline` 名称暗示它是一个通用的管线抽象，但实际上：

- **硬编码了一条 PBR 前向渲染管线**：构造函数直接加载 `shaders/pbr_vert.spv` / `shaders/pbr_frag.spv`
- **Descriptor Layout 写死了 4 个 binding**（1 UBO + 3 Combined Image Sampler）
- **没有任何 pass 实例化它来创建自己的管线**
- 唯一的"复用"是 `WaterPass` 和 `SSRPass` 调用了其静态方法 `createShaderModule()`——但该函数本质上只是一个工具函数

```cpp
// VulkanPipeline.cpp — 硬编码的 layout，不可配置
void VulkanPipeline::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;       // 写死
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // 写死
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // 写死
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // 写死
    // ...
}
```

### 3.2 各 Pass 大量重复实现相同的 Vulkan 操作

以下函数在多个 pass 中被**逐字复制**：

| 重复函数 | 重复出现的 Pass |
|----------|----------------|
| `createShaderModule()` | GBufferPass, LightingPass, ForwardPass, SSRPass, SSAOPass, NaniteDebugPass（共 **6 份**） |
| `findMemoryType()` | GBufferPass, ForwardPass, SSRPass, SSAOPass（共 **4 份**，`VulkanDevice` 中也有一份） |
| `vkCreateDescriptorSetLayout()` 裸调 | **每个 pass 都自行调用**，无统一管理 |
| `vkCreatePipelineLayout()` 裸调 | **每个 pass 都自行调用** |
| `vkCreateGraphicsPipelines()` 裸调 | GBufferPass, LightingPass, ForwardPass, WaterPass, SSRPass, NaniteDebugPass, SSAOPass |
| `vkCreateImageView()` 裸调 | GBufferPass, SSRPass, SSAOPass 等 |
| Image layout transition 裸调 | 多个 pass 各自实现 |

### 3.3 Descriptor Set Layout 缺乏统一管理

**当前状态**：
- 每个 pass 各自创建自己的 `VkDescriptorSetLayout`，互不知晓
- 没有按更新频率分层（per-frame / per-pass / per-material / per-object）
- 如果两个 pass 恰好需要相同的 layout（如都要读 GBuffer），会各自创建一份完全相同的对象
- 所有资源都挤在 `set = 0`，任何资源变化都需要重新绑定整个 set

**成熟引擎的做法**（Frequency-based Set Binding）：

```
Set 0 — Per-Frame     (相机矩阵、光照、时间等，一帧绑定一次)
Set 1 — Per-Pass      (GBuffer 纹理、Shadow Map 等，每个 pass 绑定一次)
Set 2 — Per-Material  (Albedo、Normal Map 等，换材质时绑定)
Set 3 — Per-Object    (Model 矩阵、骨骼等，每个 draw call 绑定)
```

### 3.4 缺少 Pipeline Cache

当前每次创建管线都传入 `VK_NULL_HANDLE` 作为 `VkPipelineCache`：

```cpp
vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
//                             ^^^^^^^^^^^^^^ 无缓存
```

这意味着无法跨帧/跨启动复用 pipeline 编译结果。

---

## 4. 建议的 RHI 层重构方案

### 4.1 目标架构

```
src/RHI/
├── VulkanDevice.h/cpp              ✅ 保留（核心设备管理）
├── VulkanSwapChain.h/cpp           ✅ 保留（交换链管理）
├── FrameResources.h/cpp            ✅ 保留（帧同步）
│
├── ShaderManager.h/cpp             🆕 统一 shader 加载/编译/缓存
├── DescriptorLayoutCache.h/cpp     🆕 Layout 去重缓存（相同 binding 只创建一次）
├── DescriptorAllocator.h/cpp       🆕 统一 descriptor pool 管理、自动扩容、帧末回收
├── PipelineBuilder.h/cpp           🆕 Builder 模式创建 Graphics/Compute Pipeline
├── PipelineCache.h/cpp             🆕 VkPipelineCache 管理，支持磁盘持久化
├── ImageUtils.h/cpp                🆕 统一 Image/ImageView 创建 + layout transition
├── VulkanBuffer.h/cpp              ✅ 保留并增强（统一内存分配策略）
├── VulkanTexture.h/cpp             ✅ 保留但精简（仅负责资源加载）
│
├── VulkanPipeline.h/cpp            ❌ 删除或重构
└── ComputePipeline.h/cpp           ⚠️ 合并入 PipelineBuilder
```

### 4.2 各模块设计要点

#### ShaderManager

```cpp
class ShaderManager {
public:
    // 统一的 shader 加载，自动缓存已加载的模块
    VkShaderModule getOrLoad(const std::string& spirvPath);
    
    // 帧末/销毁时统一清理
    void cleanup();
};
```

- 消灭分散在 6 个 pass 中的 `createShaderModule()` 和 `readFile()` 重复代码
- 可选：支持运行时 GLSL→SPIR-V 编译（集成 shaderc）

#### DescriptorLayoutCache

```cpp
class DescriptorLayoutCache {
public:
    // 根据 binding 描述获取或创建 layout，相同配置返回同一实例
    VkDescriptorSetLayout getOrCreate(
        const std::vector<VkDescriptorSetLayoutBinding>& bindings);
    
    void cleanup();
};
```

- 内部通过 binding 配置的 hash 去重
- 消灭各 pass 独立调用 `vkCreateDescriptorSetLayout()` 的模式

#### DescriptorAllocator

```cpp
class DescriptorAllocator {
public:
    // 从池中分配 descriptor set，池满时自动扩容
    VkDescriptorSet allocate(VkDescriptorSetLayout layout);
    
    // 帧末重置当前帧的所有分配
    void resetPools();
    
    void cleanup();
};
```

- 统一管理 descriptor pool 的创建和扩容
- 支持帧级别的批量回收

#### PipelineBuilder

```cpp
class PipelineBuilder {
public:
    // Builder 模式，链式调用
    PipelineBuilder& setShaders(VkShaderModule vert, VkShaderModule frag);
    PipelineBuilder& setVertexInput(bindingDesc, attributeDescs);
    PipelineBuilder& setRenderPass(VkRenderPass, uint32_t subpass);
    PipelineBuilder& setDepthStencil(bool test, bool write, VkCompareOp op);
    PipelineBuilder& setColorBlendAttachments(attachments);
    PipelineBuilder& setDescriptorLayouts(layouts);
    PipelineBuilder& setPushConstants(ranges);
    // ... 其他配置
    
    VkPipeline build(VkDevice device, VkPipelineCache cache);
    
    // 提供常用预设
    static PipelineBuilder fullscreenTriangle();  // 后处理 pass 常用
    static PipelineBuilder opaqueMesh();           // 几何 pass 常用
};
```

- 消灭每个 pass 中 100+ 行的 pipeline 创建样板代码
- 提供合理的默认值，只需配置差异部分

#### ImageUtils

```cpp
namespace ImageUtils {
    VkImage createImage2D(VulkanDevice* dev, uint32_t w, uint32_t h, 
                          VkFormat format, VkImageUsageFlags usage, 
                          VkDeviceMemory& outMemory);
    
    VkImageView createImageView(VkDevice dev, VkImage image, VkFormat format,
                                VkImageAspectFlags aspect, uint32_t layers = 1);
    
    void transitionLayout(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout oldLayout, VkImageLayout newLayout,
                          VkImageAspectFlags aspect, uint32_t layerCount = 1);
}
```

- 消灭 `SSAOPass` 中 6 个 `createImage*` / `createImageView*` 变体
- 消灭各 pass 中重复的 `findMemoryType()` 调用（统一走 `VulkanDevice::findMemoryType()`）

### 4.3 重构后 Pass 代码对比

**重构前**（以 `LightingPass` 为例，简化）：

```cpp
void LightingPass::init() {
    // 50 行：手动配置 descriptor layout bindings
    VkDescriptorSetLayoutBinding bindings[5] = { ... };
    vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &descriptorSetLayout);
    
    // 20 行：手动读 shader、创建 module
    auto vertCode = readFile("shaders/lighting_vert.spv");
    auto fragCode = readFile("shaders/lighting_frag.spv");
    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule fragModule = createShaderModule(fragCode);
    
    // 120 行：手动配置 pipeline 各种 state
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    // ... 省略大量样板代码
    vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    
    // 清理 shader module
    vkDestroyShaderModule(dev, vertModule, nullptr);
    vkDestroyShaderModule(dev, fragModule, nullptr);
}
```

**重构后**：

```cpp
void LightingPass::init() {
    // Descriptor layout — 一行搞定
    descriptorSetLayout = layoutCache->getOrCreate({
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  VK_SHADER_STAGE_FRAGMENT_BIT},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  VK_SHADER_STAGE_FRAGMENT_BIT},
    });
    
    // Pipeline — Builder 模式
    pipeline = PipelineBuilder::fullscreenTriangle()
        .setShaders(shaderManager->getOrLoad("shaders/lighting_vert.spv"),
                    shaderManager->getOrLoad("shaders/lighting_frag.spv"))
        .setRenderPass(renderPass, 0)
        .setDescriptorLayouts({descriptorSetLayout})
        .build(dev, pipelineCache->getCache());
}
```

---

## 5. 推荐的实施优先级

| 优先级 | 任务 | 收益 | 预估工作量 |
|--------|------|------|-----------|
| **P0** | 提取 `ShaderManager`，消灭 6 份重复的 `createShaderModule` | 消除最明显的代码重复 | 小 |
| **P0** | 将 `findMemoryType()` 统一到 `VulkanDevice`，删除各 pass 中的副本 | 消除 4 份重复 | 小 |
| **P1** | 实现 `DescriptorLayoutCache` | 统一 layout 管理，为 set 分层做准备 | 中 |
| **P1** | 实现 `PipelineBuilder` | 大幅减少各 pass 的样板代码 | 中 |
| **P2** | 实现 `DescriptorAllocator` | 统一 pool 管理，避免 pool 碎片化 | 中 |
| **P2** | 实现 `ImageUtils` | 统一 image/view 创建逻辑 | 中 |
| **P2** | 引入 `VkPipelineCache` | 加速管线创建，支持磁盘持久化 | 小 |
| **P3** | 引入 Frequency-based Set 分层 (Set 0~3) | 减少 descriptor 绑定开销 | 大 |
| **P3** | 删除/重构 `VulkanPipeline` 类 | 消除架构混乱 | 小 |

---

## 6. 参考实现

| 引擎/框架 | 相关设计 |
|-----------|---------|
| **vkguide.dev** (by V. Blanco) | `DescriptorLayoutCache` + `DescriptorAllocator` + `PipelineBuilder` 的经典教程实现 |
| **Filament** (Google) | 按 per-view / per-renderable / per-material 分层绑定 descriptor |
| **The Forge** (Confetti) | 跨 API 的 RHI 抽象，统一的 pipeline/descriptor 管理 |
| **Unreal Engine** | `BEGIN_SHADER_PARAMETER_STRUCT` 宏实现声明式 descriptor 绑定 |
| **Godot 4** | `RenderingDevice` 抽象层，统一管理 Vulkan 资源生命周期 |
