## 1. 基础准备

- [x] 1.1 创建 `src/renderer/RenderSettings.h`，将 `RenderSettings` 结构体从 `SceneRenderer.h` 提取出来，添加 `RenderMode` 枚举（Normal / WaterScene）和所有现有开关
- [x] 1.2 更新 `SceneRenderer.h`、`VulkanRenderer.h`、`DebugPanel.cpp` 的 include 指向新的 `RenderSettings.h`
- [x] 1.3 确认编译通过（仅 include 变更，无逻辑变更）

## 2. Engine 帧同步与主循环

- [x] 2.1 在 `Engine.h` 中添加帧同步成员：semaphore 数组、fence 数组、imagesInFlight、commandBuffers、currentFrame
- [x] 2.2 实现 `Engine::createSyncObjects()` — 搬迁自 `VulkanRenderer::createSyncObjects()`
- [x] 2.3 实现 `Engine::createCommandBuffers()` — 搬迁自 `VulkanRenderer::createCommandBuffers()`
- [x] 2.4 实现 `Engine::drawFrame()` — 搬迁 fence 等待、acquire image、submit、present 逻辑
- [x] 2.5 实现 `Engine::mainLoop()` — 搬迁 deltaTime 计算、FPS 统计、glfwPollEvents、processInput、drawFrame
- [x] 2.6 实现 `Engine::recreateSwapChain()` — 搬迁 swapchain 重建和所有 pass 的 resize 通知

## 3. Engine 输入系统

- [x] 3.1 在 `Engine` 中注册 GLFW 回调（mouse、scroll、key、drop、mouseButton、framebufferResize），使用 Window 回调接口
- [x] 3.2 实现 `Engine::keyCallback()` — 搬迁快捷键逻辑（5=水面、6=GPU Culling、7=Nanite、8=Clustering、9=可视化、0=调试、F1=UI、ESC=退出）
- [x] 3.3 实现 `Engine::mouseButtonCallback()` — 搬迁右键旋转、左键拾取逻辑
- [x] 3.4 实现 `Engine::mouseCallback()` / `scrollCallback()` — 搬迁相机旋转和 FOV 控制
- [x] 3.5 实现 `Engine::dropCallback()` — 搬迁 OBJ 拖放加载
- [x] 3.6 实现 `Engine::handleMousePicking()` — 搬迁射线拾取和 SelectionManager 同步
- [x] 3.7 实现 `Engine::processKeyboardInput(float dt)` — 搬迁 WASD/Space/Shift 移动

## 4. Engine 场景创建

- [x] 4.1 实现 `Engine::createDefaultScene()` — 创建 Sphere（地球纹理）、UFO（OBJ）、Plane，搬迁自 `VulkanRenderer::initVulkan()` 中间部分
- [x] 4.2 初始化 `SelectionManager` 的场景引用

## 5. SceneRenderer 完善 — 资源管理

- [x] 5.1 在 `SceneRenderer.h` 中添加所有 Pass 成员：ForwardPass、GBufferPass、LightingPass、SSAOPass、SSRPass、WaterPass、GPUDrivenRenderer、NaniteDebugPass、NaniteManager
- [x] 5.2 添加场景颜色纹理成员（sceneColorImage/Memory/View/Sampler）和辅助方法
- [x] 5.3 添加 RenderMode、renderSettings、相关开关（enableGPUCulling、enableNanite、showClusterVisualization 等）
- [x] 5.4 实现 `SceneRenderer::initDeferredShading()` — 搬迁自 `VulkanRenderer::initWaterScene()`，创建 GBuffer/Lighting/SSR/Water/SSAO/sceneColor
- [x] 5.5 实现 `SceneRenderer::cleanupDeferredShading()` — 搬迁自 `VulkanRenderer::cleanupWaterScene()`
- [x] 5.6 实现 `SceneRenderer::createSceneColorImage()` — 搬迁自同名方法

## 6. SceneRenderer 完善 — 命令录制

- [x] 6.1 实现 `SceneRenderer::recordCommands(cmd, imageIndex, frameIndex)` — 根据 renderMode 分发到两条路径
- [x] 6.2 实现 `SceneRenderer::recordForwardCommands()` — 搬迁自 `VulkanRenderer::recordCommandBuffer()`，包含 GPU Culling 和 Nanite 路径
- [x] 6.3 实现 `SceneRenderer::recordDeferredCommands()` — 搬迁自 `VulkanRenderer::recordWaterSceneCommandBuffer()`，包含 GBuffer→SSAO→Blit→SSR→Lighting→Water 完整流程
- [x] 6.4 实现 `SceneRenderer::updateUniforms()` — 搬迁 ForwardPass UBO 更新和 LightingPass/WaterPass 更新

## 7. SceneRenderer 完善 — GPU Culling & Nanite

- [x] 7.1 搬迁 `initGPUDrivenRendering()` / `cleanupGPUDrivenRendering()` / `prepareGPUCullingData()` 到 SceneRenderer
- [x] 7.2 搬迁 `initNanite()` / `cleanupNanite()` / `testNaniteClustering()` 到 SceneRenderer
- [x] 7.3 搬迁 `initNaniteDebugPass()` / `prepareNaniteCulling()` / `recordNaniteDebugCommands()` 到 SceneRenderer

## 8. Engine 初始化整合

- [x] 8.1 重写 `Engine::initializeSubsystems()` — 按顺序创建 Window → Device → SwapChain → SyncObjects → CommandBuffers → Camera → Scene → RenderSystem → SceneRenderer → UI
- [x] 8.2 重写 `Engine::shutdownSubsystems()` — 按逆序销毁所有资源（vkDeviceWaitIdle → UI → SceneRenderer → RenderSystem → Scene → Camera → Sync → SwapChain → Device → Window）
- [x] 8.3 在 `Engine::setupInputCallbacks()` 中注册所有 GLFW 回调
- [x] 8.4 UI 初始化：创建 ImGuiLayer + UIManager，传递 RenderSettings 和 Scene 引用

## 9. 入口切换

- [x] 9.1 修改 `main.cpp`：将 `VulkanRenderer renderer; renderer.run();` 替换为 `Engine engine; engine.run();`
- [x] 9.2 CMakeLists.txt 已包含 Engine/SceneRenderer 源文件（VulkanRenderer 暂保留不影响编译）
- [x] 9.3 编译并修复所有编译错误（DebugPanel.h include、NaniteManager.h include、ForwardPass 参数顺序）

## 10. 验证与清理

- [ ] 10.1 运行程序，验证前向渲染模式正常（WASD移动、鼠标旋转、左键拾取）
- [ ] 10.2 验证按 5 切换水面场景正常（延迟渲染 + SSR + Water）
- [ ] 10.3 验证 SSAO 开关正常（UI 中 checkbox 切换有效果差异）
- [ ] 10.4 验证 GPU Culling (6)、Nanite (7/8/9) 快捷键正常
- [ ] 10.5 验证 OBJ 拖放加载正常
- [ ] 10.6 验证窗口 resize 正常
- [x] 10.7 删除 `src/renderer/VulkanRenderer.h` 和 `src/renderer/VulkanRenderer.cpp`，并从 CMakeLists.txt 移除引用
- [x] 10.8 最终编译确认无报错
