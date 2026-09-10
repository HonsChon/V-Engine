## Context

当前 V-Engine 的 RHI 层（`src/RHI/`）仅封装了 Vulkan 的 Device、SwapChain、Buffer、Texture、Pipeline 等基础对象，但**没有抽象出与图形 API 无关的接口**。所有 9 个渲染 Pass 直接调用原生 Vulkan API（`vkCreateDescriptorSetLayout`、`vkCreateGraphicsPipelines`、`vkCreateRenderPass` 等），存在大量重复代码和耦合。

当前目录结构：
```
src/RHI/
├── VulkanDevice.h/cpp        # Instance/Device/Queue 管理
├── VulkanSwapChain.h/cpp     # 交换链
├── VulkanBuffer.h/cpp        # 简单 Buffer 封装
├── VulkanTexture.h/cpp       # 纹理加载（仅文件->纹理）
├── VulkanPipeline.h/cpp      # ForwardPass 专用的图形管线
├── ComputePipeline.h/cpp     # 计算管线
└── FrameResources.h/cpp      # 帧同步
```

核心痛点：
1. Pass 中有 9 份几乎相同的 `createDescriptorSetLayout()` 实现
2. Pass 中有 9 份 200+ 行的 `createPipeline()` 模板代码
3. `readFile()` + `createShaderModule()` + `findMemoryType()` 重复了 9 遍
4. 无法支持 DX12 等其他图形后端

## Goals / Non-Goals

**Goals:**
- 定义与图形 API 完全解耦的 RHI 抽象接口层，上层代码（Pass、Renderer）零直接 Vulkan 调用
- 完成 Vulkan 后端实现，功能覆盖当前所有 Pass 的需求
- 架构上预留 DX12 后端扩展点，确保接口设计兼容 DX12 的资源绑定模型（Root Signature / Descriptor Table）
- 消除 Pass 层的重复代码，Descriptor/Pipeline/RenderPass 的创建统一由 RHI 处理
- 提供 Builder 模式的管线构建 API，让管线配置可读性和复用性大幅提升

**Non-Goals:**
- 本次**不实现** DX12 后端（只做接口预留）
- 不做 VMA（Vulkan Memory Allocator）集成（可作为未来优化）
- 不做 Render Graph / Frame Graph 系统（属于更高层的调度抽象）
- 不改变 Shader 源码和编译流程（仍使用 SPIR-V）
- 不做多线程 CommandBuffer 录制（保持当前单线程模型）

## Decisions

### Decision 1: RHI 接口层架构 — 抽象基类 + 后端实现

**选择**: 使用 C++ 纯虚基类定义 RHI 接口，Vulkan/DX12 各自提供子类实现。

**替代方案**:
- **A) Pimpl + Handle 模式**（类似 Filament）：RHI 对象是 opaque handle（uint64_t），内部查表获取实际对象。优点是 ABI 稳定、编译隔离好；缺点是调试困难、类型安全弱。
- **B) 模板策略模式**：用模板参数选择后端。编译期多态零开销，但模板膨胀、编译慢。
- **C) 纯虚基类**（选择此项）：直观的 OOP 接口，类型安全，调试友好。虚函数调用开销在 GPU-bound 的渲染引擎中可忽略不计。

**理由**: 对于目前引擎的体量，纯虚基类是最清晰、最易实现和维护的方案。虚函数的微小 CPU 开销相比 GPU 工作完全可以忽略。

### Decision 2: 目录结构

**选择**:
```
src/RHI/
├── RHITypes.h              # 枚举、格式、标志位等类型定义
├── RHIDevice.h             # 抽象设备接口
├── RHICommandBuffer.h      # 抽象命令缓冲接口
├── RHIBuffer.h             # 抽象 Buffer 接口
├── RHITexture.h            # 抽象 Texture/Image 接口
├── RHISampler.h            # 抽象 Sampler 接口
├── RHIShader.h             # 抽象 Shader 接口
├── RHIDescriptor.h         # 抽象 Descriptor 接口
├── RHIPipeline.h           # 抽象 Pipeline 接口
├── RHIRenderPass.h         # 抽象 RenderPass 接口
├── RHISwapChain.h          # 抽象 SwapChain 接口
├── RHIFrameResources.h     # 帧资源管理
├── RHI.h                   # 聚合头文件 + 工厂函数
│
└── Vulkan/
    ├── VulkanRHIDevice.h/cpp
    ├── VulkanRHICommandBuffer.h/cpp
    ├── VulkanRHIBuffer.h/cpp
    ├── VulkanRHITexture.h/cpp
    ├── VulkanRHISampler.h/cpp
    ├── VulkanRHIShader.h/cpp
    ├── VulkanRHIDescriptor.h/cpp
    ├── VulkanRHIPipeline.h/cpp
    ├── VulkanRHIRenderPass.h/cpp
    ├── VulkanRHISwapChain.h/cpp
    └── VulkanRHIFrameResources.h/cpp
```

### Decision 3: RHI 类型系统 — 自定义枚举映射

**选择**: RHI 定义自己的枚举（`RHIFormat`、`RHIShaderStage`、`RHIBufferUsage` 等），在 Vulkan 后端内部做转换。

**设计**: 枚举值参考 Vulkan 的命名，但去掉 `VK_` 前缀。在 Vulkan 后端提供 `toVkFormat()`、`toVkShaderStage()` 等转换函数。未来 DX12 后端提供 `toDXGIFormat()` 等。

```cpp
// RHITypes.h
enum class RHIFormat {
    Undefined = 0,
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
    R16G16B16A16_SFLOAT,
    R32G32B32A32_SFLOAT,
    D32_SFLOAT,
    D24_UNORM_S8_UINT,
    // ...
};

enum class RHIBufferUsage : uint32_t {
    Vertex        = 1 << 0,
    Index         = 1 << 1,
    Uniform       = 1 << 2,
    Storage       = 1 << 3,
    Indirect      = 1 << 4,
    TransferSrc   = 1 << 5,
    TransferDst   = 1 << 6,
};
```

### Decision 4: Descriptor 绑定模型 — 兼容 Vulkan DescriptorSet 和 DX12 Root Signature

**选择**: RHI 层使用 "Binding Layout + Binding Group" 模型（类似 WebGPU 的 BindGroupLayout / BindGroup）。

- `RHIBindingLayout`：描述一个 set 中有哪些 binding（对应 Vulkan DescriptorSetLayout / DX12 Root Parameter）
- `RHIBindingGroup`：实际绑定的资源实例（对应 Vulkan DescriptorSet / DX12 Descriptor Table）

**理由**: 这个抽象既能映射到 Vulkan 的 DescriptorSet 模型，也能映射到 DX12 的 Root Signature + Descriptor Table 模型。比直接暴露 DescriptorSet 概念更通用。

```cpp
// 声明式绑定布局
RHIBindingLayoutDesc layoutDesc;
layoutDesc.addBinding(0, RHIDescriptorType::UniformBuffer, RHIShaderStage::VertexFragment);
layoutDesc.addBinding(1, RHIDescriptorType::CombinedImageSampler, RHIShaderStage::Fragment);
auto layout = device->createBindingLayout(layoutDesc);

// 创建绑定组
RHIBindingGroupDesc groupDesc;
groupDesc.setBuffer(0, uniformBuffer);
groupDesc.setTexture(1, albedoTexture, sampler);
auto bindingGroup = device->createBindingGroup(layout, groupDesc);

// 使用
cmd->setBindingGroup(0, bindingGroup);
```

### Decision 5: Pipeline Builder 模式

**选择**: 提供 fluent builder API 替代手动填写 100+ 行的 CreateInfo 结构体。

```cpp
auto pipeline = device->createGraphicsPipeline()
    .setVertexShader(vertShader)
    .setFragmentShader(fragShader)
    .addVertexBinding(0, sizeof(Vertex), RHIVertexInputRate::Vertex)
    .addVertexAttribute(0, 0, RHIFormat::R32G32B32_SFLOAT, offsetof(Vertex, position))
    .addVertexAttribute(0, 1, RHIFormat::R32G32B32_SFLOAT, offsetof(Vertex, normal))
    .addVertexAttribute(0, 2, RHIFormat::R32G32_SFLOAT, offsetof(Vertex, texCoord))
    .setDepthTest(true, true, RHICompareOp::Less)
    .setCullMode(RHICullMode::Back)
    .addColorAttachment(RHIFormat::R8G8B8A8_SRGB)
    .setDepthAttachment(RHIFormat::D32_SFLOAT)
    .addBindingLayout(globalLayout)
    .addBindingLayout(materialLayout)
    .addPushConstant(RHIShaderStage::Vertex, 0, sizeof(PushConstantData))
    .setRenderPass(renderPass)
    .build();
```

### Decision 6: RenderPass 抽象策略

**选择**: 保持 RenderPass 的显式抽象（不走 Dynamic Rendering），因为需要兼容 DX12。

Vulkan 有显式的 `VkRenderPass` 概念，DX12 没有（DX12 使用 render target binding + barriers）。RHI 层提供 `RHIRenderPass` 抽象来描述 attachment 和 subpass，Vulkan 后端映射到 `VkRenderPass`，未来 DX12 后端可在 begin/end 时自动管理 barriers。

```cpp
RHIRenderPassDesc desc;
desc.addColorAttachment(RHIFormat::R16G16B16A16_SFLOAT, RHILoadOp::Clear, RHIStoreOp::Store);
desc.addColorAttachment(RHIFormat::R16G16B16A16_SFLOAT, RHILoadOp::Clear, RHIStoreOp::Store);
desc.setDepthAttachment(RHIFormat::D32_SFLOAT, RHILoadOp::Clear, RHIStoreOp::Store);
auto renderPass = device->createRenderPass(desc);
```

### Decision 7: CommandBuffer 录制接口

**选择**: `RHICommandBuffer` 提供图形和计算命令的统一接口，隐藏底层 `VkCommandBuffer` / `ID3D12GraphicsCommandList`。

```cpp
// RHICommandBuffer 核心接口
class RHICommandBuffer {
public:
    virtual void beginRenderPass(RHIRenderPass* renderPass, RHIFramebuffer* framebuffer, 
                                  const std::vector<RHIClearValue>& clearValues) = 0;
    virtual void endRenderPass() = 0;
    
    virtual void bindGraphicsPipeline(RHIPipeline* pipeline) = 0;
    virtual void bindComputePipeline(RHIPipeline* pipeline) = 0;
    virtual void setBindingGroup(uint32_t set, RHIBindingGroup* group) = 0;
    
    virtual void setViewport(float x, float y, float w, float h, float minD, float maxD) = 0;
    virtual void setScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) = 0;
    
    virtual void bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset = 0) = 0;
    virtual void bindIndexBuffer(RHIBuffer* buffer, uint64_t offset, RHIIndexType type) = 0;
    
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;
    virtual void drawIndexedIndirect(RHIBuffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) = 0;
    
    virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
    virtual void dispatchIndirect(RHIBuffer* buffer, uint64_t offset) = 0;
    
    virtual void pushConstants(RHIShaderStage stages, uint32_t offset, uint32_t size, const void* data) = 0;
    
    virtual void pipelineBarrier(/* ... */) = 0;
    virtual void copyBuffer(RHIBuffer* src, RHIBuffer* dst, uint64_t size) = 0;
};
```

### Decision 8: 原生 Handle 访问 — 后门机制

**选择**: 提供 `getNativeHandle()` 方法，允许特殊场景（如 ImGui 集成）获取原生 Vulkan 对象。

```cpp
// 仅在必须与第三方库交互时使用
VkCommandBuffer vkCmd = static_cast<VulkanRHICommandBuffer*>(cmd)->getVkCommandBuffer();
```

**理由**: ImGui 的 Vulkan 后端需要原生的 `VkRenderPass`、`VkCommandBuffer` 等。完全封死原生访问不现实，但通过显式的 `static_cast` 让使用者意识到这是平台特定代码。

### Decision 9: Pure RHI 深度迁移策略（Phase 3b）

**背景**: Phase 3 完成后，所有 Pass 已通过 RHI 接口进行资源创建和管线构建，但渲染命令录制（`beginRenderPass`、`drawIndexed` 等）和材质更新仍使用 `static_cast` 下沉到 Vulkan 后端头文件（`VulkanRHI*.h`）。这意味着 Pass 的 `.cpp` 文件仍然依赖 Vulkan 后端，**无法直接支持 DX12 后端**。

**选择**: 全面移除 Pass 层对 `VulkanRHI*.h` 的依赖，达到"Pure RHI"状态：
1. **渲染命令** 全部转为 `RHICommandBuffer*` 方法（`beginRenderPass`、`bindGraphicsPipeline`、`drawIndexed`、`pushConstants` 等）
2. **材质描述符** 从 `VkDescriptorSet nativeSets[]` 改为 `std::vector<std::unique_ptr<RHIBindingGroup>> groups`，通过 `BindingGroup::updateTexture()` 更新
3. **跨 Pass 资源传递** 使用 `RHITexture*` 而非 `VkImageView`
4. **Native 桥接** 仅在 `SceneRenderer` / `NaniteManager` 等 adapter 层，使用 `RHIDevice::wrapCommandBuffer()` 将外部 VkCommandBuffer 包装为 RHI 对象

**关键模式**:
```cpp
// 材质更新 (Pure RHI)
void Pass::updateMaterialTextures(MaterialDescriptor* mat,
                                   RHITexture* albedo, RHISampler* albedoSampler, ...) {
    for (uint32_t i = 0; i < maxFrames; ++i) {
        if (!mat->groups[i]) {
            mat->groups[i] = rhiDevice_->createBindingGroup(materialLayout_.get(), desc);
        }
        mat->groups[i]->updateTexture(0, albedo, albedoSampler);
        mat->groups[i]->updateTexture(1, normal, normalSampler);
    }
    mat->valid = true;
}

// 渲染命令 (Pure RHI)
void Pass::beginRenderPass(RHICommandBuffer* cmd) {
    std::vector<RHIClearValue> clears = { ... };
    cmd->beginRenderPass(renderPass_.get(), framebuffer_.get(), clears);
    cmd->setViewport(0, 0, width_, height_, 0.0f, 1.0f);
    cmd->setScissor(0, 0, width_, height_);
}
```

**理由**: 这是实现 DX12 后端的前提条件。只有当所有 Pass 都不引用任何 `VulkanRHI*.h` 头文件时，才能在不修改 Pass 代码的情况下切换到 DX12 后端。

### Decision 10: NaniteManager 适配器模式

**选择**: `NaniteManager` 作为 Native/RHI 边界的适配器，内部仍管理 `VkCommandBuffer` 的生命周期（因为它直接操作 GPU 传输队列），但在调用 RHI 化的 culling passes 时，通过 `wrapCommandBuffer` 将 native handle 转为 RHI 对象。

```cpp
// NaniteManager 中的适配器逻辑
auto rhiCmd = m_rhiDevice->wrapCommandBuffer(static_cast<void*>(nativeCmd));
m_gpuDrivenRenderer->dispatch(rhiCmd.get(), frameIndex);
```

**理由**: NaniteManager 管理底层的 GPU 传输（`vkQueueSubmit`、fence 同步等），完全 RHI 化需要先实现 `RHIQueue` 和 `RHIFence` 抽象，属于后续迭代的范围。当前的 adapter 模式是一个务实的折中。

## Risks / Trade-offs

- **[重构范围大]** → 分阶段推进：Phase 1 实现核心 RHI + 迁移 ForwardPass 验证；Phase 2 迁移 GBufferPass + LightingPass；Phase 3 迁移其余 Pass；Phase 3b 深度纯 RHI 化。每个 Phase 确保可编译可运行。
- **[虚函数性能]** → 在 GPU-bound 的渲染引擎中，CPU 端虚函数调用的开销（每帧几百次）远小于 GPU 工作。如果未来出现瓶颈，可通过 `final` 关键字 + LTO 消除虚调用。
- **[DX12 兼容性不确定]** → RenderPass / Descriptor 模型参考了 WebGPU 和 UE5 RHI 的设计，这些都已验证过跨 Vulkan/DX12 的可行性。但实际 DX12 适配可能需要微调接口。
- **[ImGui 耦合]** → ImGui 的 Vulkan 后端需要原生 handle，通过后门机制解决，接受这部分代码的平台依赖。
- **[编译时间增加]** → 新增约 20 个头文件/源文件，但聚合头文件 `RHI.h` 可以配合前向声明减少包含链。
- **[wrapCommandBuffer 开销]** → 每次 wrap 创建一个临时 RHICommandBuffer 对象。在每帧仅调用几次的场景下完全可接受。
