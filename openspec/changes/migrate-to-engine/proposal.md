## Why

`VulkanRenderer` 是一个超过 2000 行的"上帝类"，集成了窗口管理、输入处理、Vulkan 初始化、场景创建、同步对象、前向渲染、延迟渲染（GBuffer + Lighting + SSR + Water + SSAO）、GPU-Driven Culling、Nanite 系统、UI 系统等所有功能。这使得代码难以维护、难以测试、难以扩展新功能。项目中已经存在一个预留的 `Engine` + `SceneRenderer` 新架构框架，但 `main.cpp` 仍然直接使用 `VulkanRenderer`。现在需要完成架构迁移，将 `VulkanRenderer` 的功能拆分到 `Engine` 的各个子系统中。

## What Changes

- **BREAKING**: `main.cpp` 入口从 `VulkanRenderer` 切换到 `Engine`
- 将窗口创建和输入回调迁移到 `Engine` + 现有 `Window` / `Input` 类
- 将 Vulkan 初始化（Device、SwapChain、同步对象、CommandBuffer）迁移到 `Engine::initializeSubsystems()`
- 将场景创建（ECS 实体、默认资源）迁移到 `Engine::createDefaultScene()`
- 将所有渲染逻辑（前向/延迟/水面/SSAO/SSR）迁移到 `SceneRenderer`
- 将 GPU-Driven Culling 和 Nanite 系统集成到 `SceneRenderer` 中
- 将帧同步（fence/semaphore/drawFrame）迁移到 `Engine` 或新建的 `FrameManager`
- `RenderSettings` 从 `SceneRenderer.h` 提取到独立头文件 `RenderSettings.h`
- 迁移完成后删除 `VulkanRenderer.h` / `VulkanRenderer.cpp`

## Capabilities

### New Capabilities
- `engine-lifecycle`: Engine 主循环和子系统生命周期管理（初始化、运行、关闭）
- `frame-management`: 帧同步管理（fence、semaphore、command buffer 录制与提交）
- `input-system`: 输入系统（键盘、鼠标、拖放回调统一管理）
- `scene-rendering`: SceneRenderer 调度所有渲染 Pass（前向、延迟、SSAO、SSR、Water、Nanite）

### Modified Capabilities
（无现有 spec 需要修改）

## Impact

- **入口文件**: `src/main.cpp` — 从 `VulkanRenderer` 切换到 `Engine`
- **删除文件**: `src/renderer/VulkanRenderer.h`, `src/renderer/VulkanRenderer.cpp`
- **修改文件**: `src/Application/Engine.h`, `src/Application/Engine.cpp`, `src/renderer/SceneRenderer.h`, `src/renderer/SceneRenderer.cpp`
- **新增文件**: `src/renderer/RenderSettings.h`, `src/renderer/FrameManager.h/.cpp`（可选）
- **构建**: `CMakeLists.txt` 更新源文件列表
- **依赖**: 所有 UI 面板、渲染 Pass、Nanite 系统的接口保持不变，只改调用方
