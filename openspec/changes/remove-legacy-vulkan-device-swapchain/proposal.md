## Why

引擎同时保留旧的 `VulkanDevice`/`VulkanSwapChain` 和新 RHI 抽象层，所有子系统需持有两套引用，且 Pass 层直接依赖 Vulkan 具体类阻碍了后续接入 DX12。需统一到 RHI 接口，所有 Pass **只能持有 RHI 抽象指针**，为多后端做准备。

## What Changes

- **BREAKING** 移除 `VulkanDevice` 类 (`src/RHI/VulkanDevice.h/.cpp`)
- **BREAKING** 移除 `VulkanSwapChain` 类 (`src/RHI/VulkanSwapChain.h/.cpp`)
- 补全 `RHIDevice` 抽象接口：格式查询 (`findDepthFormat`, `findSupportedFormat`)、原始 buffer/image 创建辅助、调试标记 (debug label)、sync 对象管理
- 扩展 `RHISwapChain` 抽象接口：帧生命周期 (`acquireNextImage`, `present`, `recreate`)、native handle 访问
- Engine 改为只持有 `RHIDevice*` + `RHISwapChain*`
- **所有 Pass（ForwardPass、GBufferPass、ComputePassBase、NaniteDebugPass 等）只能持有 `RHIDevice*`，禁止持有 `VulkanRHIDevice*`**
- SceneRenderer、RenderSystem、ImGuiLayer 等子系统统一使用 RHI 接口

## Capabilities

### New Capabilities
- `rhi-device-full`: RHIDevice 提供完整设备功能（格式查询、buffer/image 辅助创建、调试标记、sync 对象、command pool），完全替代旧 VulkanDevice
- `rhi-swapchain-lifecycle`: RHISwapChain 提供完整帧生命周期管理（acquire/present/recreate）及 native handle 访问

### Modified Capabilities
<!-- 无现有 spec -->

## Impact

- **代码文件**：~15+ 头/源文件需修改（所有引用 VulkanDevice 或 VulkanSwapChain 的地方）
- **API 变更**：Engine 公开接口 `getDevice()` → `getRHIDevice() → RHIDevice*`；所有 Pass 构造函数签名变更
- **架构约束**：所有 RenderPass/ComputePass 禁止直接依赖 VulkanRHI* 具体类型，为 DX12 后端预留扩展
- **构建**：CMakeLists 移除旧文件
- **子系统**：SceneRenderer、ImGuiLayer、RenderSystem、MeshManager、TextureManager、NaniteManager、GPUDrivenRenderer
