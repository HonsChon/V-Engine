## ADDED Requirements

### Requirement: Engine 初始化所有子系统
Engine SHALL 按依赖顺序初始化所有子系统：Window → VulkanDevice → VulkanSwapChain → FrameResources → Camera → Scene → RenderSystem → SceneRenderer → ImGuiLayer → UIManager。

#### Scenario: 正常启动
- **WHEN** 创建 Engine 实例并调用 run()
- **THEN** 所有子系统按顺序初始化完成，窗口显示，主循环开始

#### Scenario: 子系统初始化失败
- **WHEN** 任一子系统初始化抛出异常
- **THEN** 已创建的子系统按逆序销毁，抛出错误信息

### Requirement: Engine 主循环
Engine SHALL 运行主循环，每帧执行：轮询事件 → 处理输入 → 更新逻辑 → drawFrame（获取 image → 录制命令 → 提交 → present）→ 帧统计。

#### Scenario: 正常帧循环
- **WHEN** 主循环执行一帧
- **THEN** 依次完成事件轮询、输入处理、drawFrame、帧时间统计

#### Scenario: 窗口关闭
- **WHEN** 用户关闭窗口或按 ESC
- **THEN** 主循环结束，等待 GPU 空闲，按逆序销毁所有子系统

### Requirement: Engine 创建默认场景
Engine SHALL 在初始化后创建默认场景，包含 Sphere（地球纹理）、UFO（OBJ 模型）、Plane（蓝色平面），与当前 VulkanRenderer::initVulkan() 中的场景一致。

#### Scenario: 默认场景加载
- **WHEN** Engine 初始化完成
- **THEN** Scene 中包含 3 个实体，各自有正确的 Transform、MeshRenderer、PBRMaterial 组件
