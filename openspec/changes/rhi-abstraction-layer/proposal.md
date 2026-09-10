## Why

当前引擎的 RHI（Render Hardware Interface）层形同虚设——每个渲染 Pass（GBufferPass、ForwardPass、LightingPass、SSAOPass、SSRPass、WaterPass、NaniteDebugPass、ClusterCullingPass、FrustumCullingPass）都在直接调用原生 Vulkan API 来创建 DescriptorSetLayout、DescriptorPool、Pipeline、RenderPass、Buffer/Image 等资源。这导致了大量重复代码（每个 Pass 都重复实现 `createDescriptorSetLayout()`、`createShaderModule()`、`readFile()`、`findMemoryType()` 等），资源生命周期管理分散在各个 Pass 中极易出错，且完全无法支持跨图形 API 后端（如 DX12、Metal）。需要构建一个真正的 RHI 抽象层，以**平台无关的接口**封装底层图形 API 细节，当前实现 Vulkan 后端，架构上预留 DX12 扩展能力，让 Pass 层只通过抽象接口操作图形资源。

## What Changes

- **新增 RHI 抽象接口层**：定义与图形 API 无关的纯虚基类接口体系（`RHIDevice`、`RHICommandBuffer`、`RHIBuffer`、`RHITexture`、`RHIPipeline`、`RHIDescriptorSet`、`RHIRenderPass` 等），所有上层代码只依赖这些抽象接口
- **新增 Vulkan 后端实现**：在 `RHI/Vulkan/` 下提供所有抽象接口的 Vulkan 具体实现（`VulkanRHIDevice`、`VulkanRHIBuffer` 等），将当前散落在各 Pass 中的 Vulkan 原生调用收归此处
- **新增 RHI Descriptor 管理系统**：提供 `RHIDescriptorSetLayout`、`RHIDescriptorSet` 抽象及全局 Descriptor 池分配器，用声明式 API 替代每个 Pass 中手写的 binding 填充
- **新增 RHI Pipeline 构建器**：提供 `RHIGraphicsPipelineBuilder` 和 `RHIComputePipelineBuilder`，用 Builder 模式替代每个 Pass 中 200+ 行的管线创建代码
- **新增 RHI RenderPass/Framebuffer 抽象**：统一管理 attachment 描述、subpass 依赖，向上层提供平台无关的接口
- **新增 RHI Shader 管理**：统一的 `RHIShaderModule` 和 shader 加载，消除每个 Pass 各自实现 `readFile()` + `createShaderModule()` 的重复
- **增强资源管理**：统一的 Buffer（含 UBO 持久映射）和 Image/RenderTarget 创建接口，不再局限于纹理加载
- **新增 RHI 工厂/创建入口**：提供 `RHI::Create()` 或类似工厂，根据配置选择后端（当前只有 Vulkan，预留 DX12）
- **重构所有现有 Pass**：将 9 个 Pass 中的原生 Vulkan 调用替换为 RHI 抽象接口调用 **BREAKING**

## Capabilities

### New Capabilities
- `rhi-core-abstraction`: RHI 核心抽象接口定义（RHIDevice、RHICommandBuffer、枚举/类型映射等），与具体图形 API 完全解耦，为 Vulkan/DX12 多后端提供统一契约
- `rhi-descriptor-management`: DescriptorSetLayout / DescriptorSet / DescriptorPool 的跨后端抽象，提供声明式绑定描述和自动池管理
- `rhi-pipeline-builder`: 图形管线和计算管线的 Builder 模式构建器，封装 shader 加载、顶点输入、光栅化、混合等配置，接口与底层 API 无关
- `rhi-renderpass`: RenderPass 和 Framebuffer 的抽象封装，声明式定义 attachment 和 subpass
- `rhi-resource-management`: 统一的 Buffer 和 Image 资源创建/管理接口，涵盖 UBO 持久映射、render target、sampler 等场景

### Modified Capabilities
<!-- 无现有 spec，首次建立 -->

## Impact

- **代码结构**：`src/RHI/` 重组为 `src/RHI/Interface/`（抽象接口）+ `src/RHI/Vulkan/`（Vulkan 实现），新增约 15-20 个文件
- **Pass 层重构**：`src/renderer/passes/` 下所有 9 个 Pass 需要重构，移除原生 Vulkan 调用，改用 RHI 接口
- **Pass 基类调整**：`RenderPassBase`、`ComputePassBase` 的接口需要从依赖 `VkCommandBuffer` 改为 `RHICommandBuffer`
- **类型系统**：需要定义 RHI 层自己的枚举（Format、ShaderStage、BufferUsage 等），与 Vulkan/DX12 类型做映射
- **依赖**：不引入新的外部依赖，仍基于 Vulkan SDK；DX12 后端为未来扩展，当前不实现
- **构建系统**：CMakeLists.txt 需要调整以支持新的目录结构和条件编译
- **风险**：重构范围大，建议分阶段推进——先实现核心抽象 + Vulkan 后端并让一个 Pass（如 ForwardPass）迁移验证，再批量迁移其他 Pass
