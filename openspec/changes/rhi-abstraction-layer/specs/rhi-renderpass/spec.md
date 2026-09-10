## ADDED Requirements

### Requirement: 声明式 RenderPass 创建
RHIDevice SHALL 提供 `createRenderPass(RHIRenderPassDesc)` 方法。`RHIRenderPassDesc` SHALL 支持通过 `addColorAttachment(format, loadOp, storeOp, initialLayout, finalLayout)` 和 `setDepthAttachment(format, loadOp, storeOp, initialLayout, finalLayout)` 声明式定义附件。

#### Scenario: 创建 GBuffer RenderPass
- **WHEN** 创建 RHIRenderPassDesc，添加 3 个颜色附件（Position: R16G16B16A16_SFLOAT、Normal: R16G16B16A16_SFLOAT、Albedo: R8G8B8A8_UNORM）和 1 个深度附件（D32_SFLOAT），全部 loadOp=Clear、storeOp=Store，然后调用 `device->createRenderPass(desc)`
- **THEN** SHALL 成功创建 RHIRenderPass，Vulkan 后端内部对应 VkRenderPass，自动设置 subpass 依赖

#### Scenario: 创建简单的后处理 RenderPass
- **WHEN** 只添加 1 个颜色附件（format 与 swapchain 一致）、无深度附件
- **THEN** SHALL 成功创建 RHIRenderPass

### Requirement: RenderPass 附件 Layout 管理
RHI 后端 SHALL 根据 loadOp/storeOp 和附件类型自动推断或允许指定初始/最终 image layout。Vulkan 后端映射到 `VkImageLayout`，DX12 后端未来映射到 resource state transition。

#### Scenario: 颜色附件 Layout 自动推断
- **WHEN** addColorAttachment 时未显式指定 layout
- **THEN** SHALL 使用合理默认值：initialLayout = Undefined，finalLayout = ShaderReadOnly

#### Scenario: 深度附件 Layout
- **WHEN** setDepthAttachment 指定 finalLayout 为 DepthStencilReadOnly
- **THEN** 在 Vulkan 后端 SHALL 映射到 VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL

### Requirement: Subpass 依赖自动管理
对于单 subpass 的 RenderPass（当前引擎所有 Pass 均为单 subpass），Vulkan 后端 SHALL 自动添加 EXTERNAL→subpass0 和 subpass0→EXTERNAL 的依赖关系，上层代码 SHALL NOT 需要手动配置 `VkSubpassDependency`。

#### Scenario: 单 subpass 默认依赖
- **WHEN** 创建一个包含颜色和深度附件的 RenderPass（单 subpass）
- **THEN** Vulkan 后端 SHALL 自动生成正确的前后 subpass 依赖（srcStageMask/dstStageMask、srcAccessMask/dstAccessMask）

### Requirement: Framebuffer 创建
RHIDevice SHALL 提供 `createFramebuffer(RHIFramebufferDesc)` 方法。`RHIFramebufferDesc` SHALL 包含关联的 RenderPass、附件列表（RHITexture 的 ImageView）、宽度和高度。

#### Scenario: 创建 GBuffer Framebuffer
- **WHEN** 创建 RHIFramebufferDesc，设置关联的 RenderPass、4 个 RHITexture（position、normal、albedo、depth）、尺寸为 1920x1080，然后 `createFramebuffer(desc)`
- **THEN** SHALL 成功创建 RHIFramebuffer，Vulkan 后端内部对应 VkFramebuffer

#### Scenario: Framebuffer 尺寸与附件一致性
- **WHEN** Framebuffer 指定的宽高与附件纹理的尺寸不匹配
- **THEN** SHALL 报告错误或验证失败

### Requirement: RenderPass 在 CommandBuffer 中使用
`RHICommandBuffer::beginRenderPass(renderPass, framebuffer, clearValues)` SHALL 开始一个渲染通道，`endRenderPass()` SHALL 结束。clearValues 使用 RHI 自定义的 `RHIClearValue`（支持颜色和深度/模板值）。

#### Scenario: 使用 RenderPass 录制 GBuffer 绘制
- **WHEN** 调用 `cmd->beginRenderPass(gbufferRenderPass, gbufferFramebuffer, {clearColor, clearColor, clearColor, clearDepth})`，然后绑定管线绘制，最后 `cmd->endRenderPass()`
- **THEN** Vulkan 后端 SHALL 正确调用 vkCmdBeginRenderPass / vkCmdEndRenderPass

### Requirement: RenderPass 资源生命周期
RHIRenderPass 和 RHIFramebuffer 对象销毁时 SHALL 自动清理底层原生资源。

#### Scenario: 窗口 resize 时重建 Framebuffer
- **WHEN** 窗口尺寸变化，销毁旧的 RHIFramebuffer 并以新尺寸创建新的
- **THEN** 旧的 Framebuffer 底层 VkFramebuffer SHALL 被正确销毁，新的 SHALL 被正确创建
