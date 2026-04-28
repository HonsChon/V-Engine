## ADDED Requirements

### Requirement: RHI 接口与图形 API 完全解耦
所有 RHI 抽象接口（RHIDevice、RHICommandBuffer、RHIBuffer、RHITexture、RHIPipeline 等）SHALL 定义为纯虚基类，不包含任何 Vulkan / DX12 特定的头文件引用或类型。上层代码（Pass、Renderer）SHALL 仅通过这些抽象接口操作图形资源。

#### Scenario: 上层代码无 Vulkan 依赖
- **WHEN** 编译 `src/renderer/passes/` 下的任意 Pass 源文件
- **THEN** 该源文件 SHALL NOT 包含 `<vulkan/vulkan.h>` 或任何 `VK_` 前缀类型，仅包含 RHI 抽象头文件

#### Scenario: 抽象接口无平台类型泄漏
- **WHEN** 检查 `src/RHI/` 根目录下的所有抽象接口头文件
- **THEN** 这些头文件 SHALL NOT 包含 `<vulkan/vulkan.h>`、`<d3d12.h>` 或任何平台特定图形 API 的头文件

### Requirement: RHI 类型系统
RHI SHALL 定义独立的枚举类型体系，包括但不限于：`RHIFormat`（像素/顶点格式）、`RHIBufferUsage`（Buffer 用途标志位）、`RHITextureUsage`（Texture 用途标志位）、`RHIShaderStage`（着色器阶段）、`RHICompareOp`（比较操作）、`RHICullMode`（剔除模式）、`RHILoadOp` / `RHIStoreOp`（附件加载/存储操作）、`RHIIndexType`（索引类型）。

#### Scenario: Format 枚举覆盖现有用例
- **WHEN** 查看 `RHIFormat` 枚举定义
- **THEN** SHALL 包含当前引擎中使用的所有格式：R8G8B8A8_UNORM、R8G8B8A8_SRGB、R16G16B16A16_SFLOAT、R32G32B32_SFLOAT、R32G32B32A32_SFLOAT、R32G32_SFLOAT、D32_SFLOAT、D24_UNORM_S8_UINT

#### Scenario: 标志位支持按位组合
- **WHEN** 使用 `RHIBufferUsage` 或 `RHITextureUsage`
- **THEN** SHALL 支持按位或（`|`）组合多个标志（如 `RHIBufferUsage::Vertex | RHIBufferUsage::TransferDst`）

### Requirement: RHIDevice 作为核心工厂接口
`RHIDevice` SHALL 作为所有 RHI 资源创建的核心工厂，提供创建 Buffer、Texture、Sampler、Shader、BindingLayout、BindingGroup、Pipeline、RenderPass、Framebuffer 的方法。

#### Scenario: 通过 RHIDevice 创建 Buffer
- **WHEN** 调用 `device->createBuffer(desc)` 传入 RHIBufferDesc
- **THEN** SHALL 返回一个 `std::unique_ptr<RHIBuffer>` 或 `std::shared_ptr<RHIBuffer>`，底层由当前后端实现分配

#### Scenario: 通过 RHIDevice 创建 Pipeline
- **WHEN** 调用 `device->createGraphicsPipeline()` 
- **THEN** SHALL 返回一个 Pipeline Builder 对象，通过链式调用配置后 `build()` 生成 `RHIPipeline`

### Requirement: RHICommandBuffer 录制接口
`RHICommandBuffer` SHALL 提供渲染和计算命令的录制接口，包括：beginRenderPass / endRenderPass、bindGraphicsPipeline / bindComputePipeline、setBindingGroup、setViewport / setScissor、bindVertexBuffer / bindIndexBuffer、draw / drawIndexed / drawIndexedIndirect、dispatch / dispatchIndirect、pushConstants、pipelineBarrier。

#### Scenario: 录制一个完整的绘制调用
- **WHEN** 在 RHICommandBuffer 上依次调用 beginRenderPass → bindGraphicsPipeline → setBindingGroup → setViewport → setScissor → bindVertexBuffer → bindIndexBuffer → drawIndexed → endRenderPass
- **THEN** 后端 SHALL 将这些调用正确转换为对应图形 API 的原生命令

#### Scenario: 录制计算分派
- **WHEN** 调用 bindComputePipeline → setBindingGroup → dispatch
- **THEN** 后端 SHALL 正确执行计算着色器分派

### Requirement: 后端工厂函数
SHALL 提供全局工厂函数（如 `RHI::CreateDevice()`），根据编译配置或运行时参数选择创建 Vulkan 或 DX12 后端的 RHIDevice 实例。当前仅需实现 Vulkan 后端。

#### Scenario: 创建 Vulkan 后端
- **WHEN** 调用 `RHI::CreateDevice(RHIBackend::Vulkan, window)` 
- **THEN** SHALL 返回一个 VulkanRHIDevice 实例（向上转型为 RHIDevice）

#### Scenario: DX12 后端未实现时的行为
- **WHEN** 调用 `RHI::CreateDevice(RHIBackend::DX12, window)` 且 DX12 后端尚未实现
- **THEN** SHALL 抛出异常或返回错误，提示 DX12 后端尚未可用

### Requirement: 原生 Handle 后门访问
每个 RHI 对象的后端实现 SHALL 提供获取原生 Handle 的方法（如 `getVkBuffer()`、`getVkCommandBuffer()`），用于与第三方库（如 ImGui Vulkan 后端）集成。此方法仅在后端具体类型上可用，不出现在抽象接口中。

#### Scenario: ImGui 获取原生 CommandBuffer
- **WHEN** 需要将 RHICommandBuffer 传递给 ImGui Vulkan 后端
- **THEN** 可通过 `static_cast<VulkanRHICommandBuffer*>(cmd)->getVkCommandBuffer()` 获取原生 `VkCommandBuffer`
