## ADDED Requirements

### Requirement: SceneRenderer 双模式渲染
SceneRenderer SHALL 支持两种渲染模式：Normal（前向渲染）和 WaterScene（延迟渲染 + SSR + Water），通过 RenderSettings 中的 renderMode 控制。

#### Scenario: 前向渲染模式
- **WHEN** renderMode == Normal
- **THEN** 执行 ForwardPass 渲染（可选 GPU Culling / Nanite），然后渲染 UI

#### Scenario: 延迟渲染模式
- **WHEN** renderMode == WaterScene
- **THEN** 按顺序执行 GBuffer → SSAO → Blit → SSR → Lighting → Water → UI

### Requirement: SceneRenderer 管理所有 RenderPass
SceneRenderer SHALL 拥有并管理以下 Pass 的生命周期：ForwardPass、GBufferPass、LightingPass、SSAOPass、SSRPass、WaterPass、GPUDrivenRenderer、NaniteDebugPass。延迟渲染相关 Pass 支持延迟初始化。

#### Scenario: 延迟初始化
- **WHEN** 用户首次切换到 WaterScene 模式
- **THEN** SceneRenderer 创建 GBuffer、Lighting、SSR、Water、SSAO、sceneColorImage 等资源

#### Scenario: 窗口 resize
- **WHEN** swapchain 重建
- **THEN** SceneRenderer 重建所有分辨率相关资源（GBuffer、SSAO、SSR framebuffer 等）

### Requirement: SceneRenderer 录制命令
SceneRenderer SHALL 提供 `recordCommands(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frameIndex)` 方法，在给定的命令缓冲上录制完整的渲染命令序列（根据当前 renderMode）。

#### Scenario: Normal 模式命令录制
- **WHEN** recordCommands 在 Normal 模式下被调用
- **THEN** 录制 GPU Culling（可选）→ BeginRenderPass → ForwardPass/Nanite → UI → EndRenderPass

#### Scenario: WaterScene 模式命令录制
- **WHEN** recordCommands 在 WaterScene 模式下被调用
- **THEN** 录制 GBuffer → SSAO → Blit → SSR → BeginRenderPass → Lighting → Water → UI → EndRenderPass

### Requirement: SceneRenderer 更新 Uniform
SceneRenderer SHALL 每帧更新 ForwardPass UBO（view/proj/light）和 LightingPass UBO（相机位置/光源），以及 WaterPass uniform（如果在 WaterScene 模式）。

#### Scenario: 每帧 Uniform 更新
- **WHEN** Engine 调用 SceneRenderer 的更新方法
- **THEN** ForwardPass 和 LightingPass 的 UBO 使用当前相机和光照参数更新
