## Context

项目当前使用 `VulkanRenderer`（~2000行）作为唯一入口，它是一个"上帝类"，负责窗口、输入、Vulkan 设备、场景、渲染（前向+延迟+SSAO+SSR+Water）、GPU Culling、Nanite、UI 等全部功能。同时项目中已存在一个未启用的 `Engine` 类框架（在 `src/Application/`），它把子系统拆分为独立的 `unique_ptr` 成员，设计更清晰但功能不完整。

**当前架构**：
```
main.cpp → VulkanRenderer (上帝类，包含一切)
```

**目标架构**：
```
main.cpp → Engine
             ├─ Window (GLFW 窗口)
             ├─ Input (键盘/鼠标/拖放)
             ├─ VulkanDevice
             ├─ VulkanSwapChain
             ├─ FrameResources (同步对象 + CommandBuffer)
             ├─ Camera
             ├─ Scene (ECS)
             ├─ RenderSystem (材质/网格管理)
             ├─ SceneRenderer (调度所有 Pass)
             │    ├─ ForwardPass
             │    ├─ GBufferPass
             │    ├─ LightingPass
             │    ├─ SSAOPass
             │    ├─ SSRPass
             │    ├─ WaterPass
             │    ├─ GPUDrivenRenderer
             │    └─ NaniteDebugPass
             ├─ ImGuiLayer
             └─ UIManager
```

## Goals / Non-Goals

**Goals:**
- 将 `VulkanRenderer` 的所有功能无损迁移到 `Engine` + `SceneRenderer` 架构
- 保持渲染结果完全一致（像素级不变）
- 所有快捷键、鼠标交互、拖放加载、UI 面板功能保持不变
- 迁移后删除 `VulkanRenderer`，`main.cpp` 仅使用 `Engine`

**Non-Goals:**
- 不引入新的渲染功能或优化
- 不重构各个 RenderPass 的内部实现
- 不改变 ECS/Scene/Component 的结构
- 不做多线程命令录制

## Decisions

### 1. 帧同步放在 Engine 还是 SceneRenderer？
**决定**：放在 `Engine` 中（或新建 `FrameResources` 辅助类）。
**理由**：帧同步（fence/semaphore）、`drawFrame()` 流程、swapchain image 获取属于框架级职责，不属于渲染调度。`SceneRenderer` 只负责在给定的 command buffer 上录制渲染命令。
**替代方案**：放在 SceneRenderer — 但这会让 SceneRenderer 承担过多职责。

### 2. 渲染模式（Normal vs WaterScene）如何处理？
**决定**：`SceneRenderer` 通过 `RenderSettings` 中的模式标志控制渲染路径。
**理由**：VulkanRenderer 中的 `RenderMode` 枚举直接搬到 `RenderSettings` 中，SceneRenderer 在 `render()` 中根据模式选择不同的命令录制路径。

### 3. 输入回调如何迁移？
**决定**：`Engine` 注册 GLFW 回调，转发到各子系统（Camera、SelectionManager、UI）。
**理由**：Engine.h 已有 `Input` 和 `setupInputCallbacks()` 的预留。键盘/鼠标事件统一在 Engine 层分发，避免子系统直接访问 GLFW。

### 4. RenderSettings 放在哪里？
**决定**：提取到独立的 `src/renderer/RenderSettings.h`。
**理由**：`VulkanRenderer.h`、`DebugPanel.cpp`、`SceneRenderer.h` 都需要引用它，放在独立文件避免循环依赖。

### 5. 迁移策略：一步到位还是渐进式？
**决定**：一步到位。因为 `Engine` 框架已经搭好，主要工作是"搬代码"而非"设计新接口"。渐进式会导致两套入口并存的混乱状态。

## Risks / Trade-offs

- **[功能遗漏]** VulkanRenderer 中有很多细节逻辑（如 Nanite readback timing、GPU Culling 数据准备），搬迁时可能遗漏 → **缓解**：逐函数对照搬迁，搬完后对比两个文件确保无遗漏
- **[编译问题]** 大量 include 路径和前向声明需要调整 → **缓解**：分步编译，先确保头文件通过，再填充实现
- **[运行时崩溃]** 初始化顺序改变可能导致空指针 → **缓解**：严格保持 VulkanRenderer 中的初始化顺序
- **[回退困难]** 一步到位意味着无法部分回退 → **缓解**：在 Git 分支上操作，保留 VulkanRenderer 文件直到验证通过
