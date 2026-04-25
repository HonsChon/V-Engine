## ADDED Requirements

### Requirement: 键盘输入处理
Engine SHALL 注册 GLFW 键盘回调，处理 WASD 移动、Space/Shift 上下、ESC 退出、F1 切换 UI、数字键切换功能（5=水面, 6=GPU Culling, 7=Nanite, 8=测试 Clustering, 9=可视化, 0=调试模式）。

#### Scenario: 相机移动
- **WHEN** 用户按下 WASD/Space/Shift
- **THEN** Camera 对应方向移动，速度与 deltaTime 成正比

#### Scenario: 功能切换
- **WHEN** 用户按下数字键 5-9
- **THEN** 对应功能开关状态翻转，必要时触发延迟初始化

### Requirement: 鼠标输入处理
Engine SHALL 注册 GLFW 鼠标回调：右键按下启用相机旋转（隐藏光标），右键释放停止旋转（恢复光标），左键触发射线拾取，滚轮控制 FOV。

#### Scenario: 相机旋转
- **WHEN** 用户按住右键并移动鼠标
- **THEN** Camera 根据鼠标偏移量旋转，光标被隐藏

#### Scenario: 射线拾取
- **WHEN** 用户左键点击（且 ImGui 未捕获鼠标）
- **THEN** 执行 Ray-AABB 检测，命中实体则选中并同步到 UI 面板

### Requirement: 拖放加载
Engine SHALL 注册 GLFW drop 回调，支持拖入 .obj 文件自动创建新 ECS 实体。

#### Scenario: 拖入 OBJ 文件
- **WHEN** 用户将 .obj 文件拖入窗口
- **THEN** 创建新实体，添加 MeshRendererComponent 指向该文件路径
