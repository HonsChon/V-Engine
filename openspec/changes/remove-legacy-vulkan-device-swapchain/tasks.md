## 1. 扩展 RHI 抽象接口

- [x] 1.1 扩展 `RHISwapChain.h`：添加 `RHISwapChainResult` 枚举、`acquireNextImage()`、`present()`、`recreate()`、`getNativeRenderPass()`、`getNativeFramebuffer()` 纯虚方法
- [x] 1.2 扩展 `RHIDevice.h`：添加格式查询 (`findSupportedFormat`, `findDepthFormat`)、raw buffer/image 创建、copyBuffer、single-time commands、debug label、sync 对象管理、command buffer 分配、queue submit、native handle access、`createSwapChain()` 工厂方法

## 2. 实现 VulkanRHIDevice 新增方法

- [x] 2.1 在 `VulkanRHIDevice.h/.cpp` 实现 `findSupportedFormat()` / `findDepthFormat()`
- [x] 2.2 在 `VulkanRHIDevice.h/.cpp` 实现 `createRawBuffer()` / `createRawImage()` / `copyBuffer()`
- [x] 2.3 在 `VulkanRHIDevice.h/.cpp` 实现 `beginSingleTimeCommands()` / `endSingleTimeCommands()`
- [x] 2.4 在 `VulkanRHIDevice.h/.cpp` 实现 `beginDebugLabel()` / `endDebugLabel()` / `insertDebugLabel()`
- [x] 2.5 在 `VulkanRHIDevice.h/.cpp` 实现 sync 对象方法 (`createSemaphore`, `createFence`, `destroySemaphore`, `destroyFence`, `waitForFence`, `resetFence`)
- [x] 2.6 在 `VulkanRHIDevice.h/.cpp` 实现 `allocateCommandBuffers()` / `submitGraphicsQueue()`
- [x] 2.7 在 `VulkanRHIDevice.h/.cpp` 实现 native handle 访问 (`getNativeDevice`, `getNativeInstance`, `getNativePhysicalDevice`, `getGraphicsQueueFamilyIndex`, `getNativeGraphicsQueue`)
- [x] 2.8 在 `VulkanRHIDevice.h/.cpp` 实现 `createSwapChain(width, height)`

## 3. 实现 VulkanRHISwapChain 新增方法

- [x] 3.1 修改 `VulkanRHISwapChain.h/.cpp` 实现 `acquireNextImage(void*, uint32_t*)` → 包装 VkResult 为 RHISwapChainResult
- [x] 3.2 修改 `VulkanRHISwapChain.h/.cpp` 实现 `present(void*, uint32_t)` → 包装 VkResult
- [x] 3.3 修改 `VulkanRHISwapChain.h/.cpp` 标记 `recreate()` 为 override
- [x] 3.4 实现 `getNativeRenderPass()` / `getNativeFramebuffer()`

## 4. 重构 Engine

- [x] 4.1 修改 `Engine.h`：移除 `VulkanDevice`/`VulkanSwapChain` 成员，改为 `unique_ptr<RHIDevice>` + `unique_ptr<RHISwapChain>`；移除旧 getter，保留 `getRHIDevice()` / `getRHISwapChain()`
- [x] 4.2 修改 `Engine.cpp` 初始化：用 `VulkanRHIDevice::createStandalone(window)` 创建设备，再通过 `m_rhiDevice->createSwapChain()` 创建交换链
- [x] 4.3 修改 `Engine.cpp` 帧循环 (`drawFrame`)：所有 sync/acquire/present/submit 调用切到 `RHIDevice`/`RHISwapChain` 方法
- [x] 4.4 修改 `Engine.cpp` `createSyncObjects()` / `createCommandBuffers()`：使用 `m_rhiDevice->createSemaphore()` 等
- [x] 4.5 修改 `Engine.cpp` `recreateSwapChain()`：使用 `m_rhiSwapChain->recreate()`
- [x] 4.6 修改 `Engine.cpp` ImGui 初始化：通过 `m_rhiDevice->getNative*()` 和 `m_rhiSwapChain->getNativeRenderPass()` 获取参数
- [x] 4.7 修改 `Engine.cpp` `shutdownSubsystems()`：删除旧清理逻辑，使用 RHI 方法

## 5. 重构 SceneRenderer 和 Pass 层

- [x] 5.1 修改 `SceneRenderer` 构造函数：从 `(VulkanDevice*, VulkanSwapChain*)` 改为 `(RHIDevice*, RHISwapChain*)`
- [x] 5.2 修改 `RenderPassBase`：移除所有 `VulkanDevice*`/`shared_ptr<VulkanDevice>` 引用，只保留 `RHIDevice*`
- [x] 5.3 修改 `ForwardPass`：构造参数改为 `RHIDevice*`，移除 Vulkan 具体类 include
- [x] 5.4 修改 `GBufferPass`：同上
- [x] 5.5 修改 `ComputePassBase` 及子类：同上
- [x] 5.6 修改 `NaniteDebugPass`：同上
- [x] 5.7 修改 `GPUDrivenRenderer`：同上

## 6. 重构其他子系统

- [x] 6.1 修改 `RenderSystem`：构造/init 从 `shared_ptr<VulkanDevice>` 改为 `RHIDevice*`
- [x] 6.2 修改 `MeshManager`：已经是 RHI 化（无需改动）
- [x] 6.3 修改 `TextureManager`：移除 VulkanDevice 前向声明和遗留兼容接口
- [x] 6.4 修改 `ImGuiLayer`：构造参数从多个 Vk handle 改为 `(GLFWwindow*, RHIDevice*, RHISwapChain*)`
- [x] 6.5 修改 `NaniteManager`：移除 VulkanDevice 引用，改为纯 RHIDevice*

## 7. 清理旧代码

- [ ] 7.1 删除 `src/RHI/VulkanDevice.h` 和 `src/RHI/VulkanDevice.cpp`
- [ ] 7.2 删除 `src/RHI/VulkanSwapChain.h` 和 `src/RHI/VulkanSwapChain.cpp`
- [ ] 7.3 更新 `CMakeLists.txt`：移除旧文件引用
- [x] 7.4 全局搜索确认无残留 — Pass/子系统层已无残留，仅余后端实现 `VulkanRHIDevice.cpp` 的包装构造函数（合理保留或随 7.1 一并清除）

## 8. 验证

- [ ] 8.1 编译通过（无错误、无 Vulkan 具体类泄露到 Pass 层）
- [ ] 8.2 运行引擎，确认渲染正常（前向渲染、SwapChain resize）
- [ ] 8.3 确认 ImGui 正常工作
- [ ] 8.4 确认 RenderDoc debug label 功能正常
