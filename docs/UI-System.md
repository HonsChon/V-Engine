# V Engine UI 系统文档

## 概述

V Engine 的 UI 系统基于 **Dear ImGui** 构建，使用其 **Docking 分支** 来实现专业级的编辑器界面。整个 UI 系统与 Vulkan 渲染管线深度集成，支持窗口停靠、面板拖拽、实时属性编辑等现代游戏引擎编辑器的标准功能。

## 架构概览

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          VulkanRenderer                                  │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                         ImGuiLayer                                  │ │
│  │  • Vulkan/GLFW 后端封装                                             │ │
│  │  • 管理 ImGui 生命周期                                              │ │
│  │  • 处理 DockSpace 创建                                              │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                         UIManager                                   │ │
│  │  • 协调所有 UI 面板                                                 │ │
│  │  • 管理面板可见性                                                   │ │
│  │  • 提供数据更新接口                                                 │ │
│  │  ┌──────────────┬──────────────┬──────────────┬──────────────────┐  │ │
│  │  │  DebugPanel  │  Hierarchy   │  Inspector   │  AssetBrowser    │  │ │
│  │  │              │    Panel     │    Panel     │     Panel        │  │ │
│  │  └──────────────┴──────────────┴──────────────┴──────────────────┘  │ │
│  └────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

## 文件结构

```
src/ui/
├── ImGuiLayer.h/.cpp           # ImGui Vulkan/GLFW 后端封装
├── UIManager.h/.cpp            # UI 面板管理器
└── panels/
    ├── DebugPanel.h/.cpp       # 调试信息面板
    ├── SceneHierarchyPanel.h/.cpp  # 场景层级面板
    ├── InspectorPanel.h/.cpp   # 属性检查器面板
    └── AssetBrowserPanel.h/.cpp    # 资源浏览器面板
```

---

## 核心组件

### 1. ImGuiLayer

**文件**: `src/ui/ImGuiLayer.h/.cpp`

ImGuiLayer 是 ImGui 与 Vulkan 渲染管线之间的桥梁，负责：

- ImGui 的初始化和资源管理
- 创建全屏 DockSpace（停靠空间）
- 每帧的 UI 渲染命令录制

#### 关键方法

| 方法 | 描述 |
|------|------|
| `init()` | 初始化 ImGui Vulkan 后端 |
| `beginFrame()` | 开始新的 ImGui 帧 |
| `endFrame()` | 结束帧并录制渲染命令 |
| `onResize()` | 处理窗口大小改变 |
| `cleanup()` | 释放所有 ImGui 资源 |

#### 初始化流程

```cpp
// 在 VulkanRenderer::initUI() 中
imguiLayer = std::make_unique<ImGuiLayer>(
    window,                           // GLFW 窗口
    device->getInstance(),            // Vulkan 实例
    device->getPhysicalDevice(),      // 物理设备
    device->getDevice(),              // 逻辑设备
    device->getGraphicsQueueFamily(), // 队列族索引
    device->getGraphicsQueue(),       // 图形队列
    swapChain->getRenderPass(),       // 目标 RenderPass
    swapChain->getImageCount()        // 交换链图像数量
);
```

#### 渲染流程

```
每帧渲染循环:
┌─────────────────────────────────────────┐
│ vkCmdBeginRenderPass(...)               │  ← 开始主 RenderPass
├─────────────────────────────────────────┤
│ // ... 渲染 3D 场景 ...                  │
├─────────────────────────────────────────┤
│ imguiLayer->beginFrame()                │  ← 开始 ImGui 帧
│   ├─ ImGui_ImplVulkan_NewFrame()        │
│   ├─ ImGui_ImplGlfw_NewFrame()          │
│   ├─ ImGui::NewFrame()                  │
│   └─ 创建 DockSpace（如果启用）           │
├─────────────────────────────────────────┤
│ uiManager->render()                     │  ← 渲染所有 UI 面板
├─────────────────────────────────────────┤
│ imguiLayer->endFrame(commandBuffer)     │  ← 结束并录制命令
│   ├─ ImGui::Render()                    │
│   └─ ImGui_ImplVulkan_RenderDrawData()  │
├─────────────────────────────────────────┤
│ vkCmdEndRenderPass(commandBuffer)       │  ← 结束 RenderPass
└─────────────────────────────────────────┘
```

---

### 2. DockSpace 系统

DockSpace 是 ImGui Docking 分支的核心功能，它创建一个可停靠窗口的容器区域。

#### DockSpace 创建代码

```cpp
// 在 ImGuiLayer::beginFrame() 中
if (dockingEnabled) {
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;
    
    // 获取主视口信息
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    
    // 配置窗口属性：全屏、无边框、透明
    windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
    windowFlags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    windowFlags |= ImGuiWindowFlags_NoBackground;

    // 设置样式：无圆角、无边框、无内边距
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::Begin("DockSpace", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    // 创建停靠空间
    ImGuiID dockspaceId = ImGui::GetID("VEngineDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), 
                     ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}
```

#### 窗口标志说明

| 标志 | 作用 |
|------|------|
| `NoTitleBar` | 无标题栏 |
| `NoCollapse` | 禁止折叠 |
| `NoResize` | 禁止调整大小 |
| `NoMove` | 禁止移动 |
| `NoBackground` | 透明背景（可见 3D 场景） |
| `NoBringToFrontOnFocus` | 点击不置顶 |
| `NoNavFocus` | 键盘导航跳过 |

#### PassthruCentralNode 标志

`ImGuiDockNodeFlags_PassthruCentralNode` 是关键标志：

- **作用**: 使中央节点透明，鼠标事件可穿透
- **效果**: 中央区域可以看到并交互 3D 场景

```
使用 PassthruCentralNode:
┌─────────────────────────────────────────┐
│  ┌─────────┐                ┌─────────┐ │
│  │Hierarchy│                │Inspector│ │
│  │  Panel  │   3D Scene     │  Panel  │ │
│  │ (停靠)   │   (透明可见)   │ (停靠)  │ │
│  └─────────┘                └─────────┘ │
│              ↑ 鼠标可交互 3D 场景         │
└─────────────────────────────────────────┘
```

---

### 3. UIManager

**文件**: `src/ui/UIManager.h/.cpp`

UIManager 是所有 UI 面板的协调者，负责：

- 创建和管理所有面板实例
- 控制面板可见性
- 提供数据更新接口
- 渲染主菜单栏

#### 数据结构

```cpp
// 渲染统计信息
struct RenderStats {
    float fps = 0.0f;
    float frameTime = 0.0f;      // 毫秒
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t vertices = 0;
    size_t gpuMemoryUsed = 0;    // 字节
};

// 场景信息
struct SceneInfo {
    std::string currentSceneName = "Untitled";
    int objectCount = 0;
    int lightCount = 0;
    bool isWaterScene = false;
    bool isDeferredMode = false;
};
```

#### 使用示例

```cpp
// 更新调试面板数据
auto* debugPanel = uiManager->getDebugPanel();
if (debugPanel) {
    debugPanel->setFPS(fps);
    debugPanel->setFrameTime(deltaTime * 1000.0f);
    debugPanel->setCameraPosition(camera->getPosition());
    debugPanel->setVertices(mesh->getVertices().size());
}

// 切换面板可见性
uiManager->toggleDebugPanel();
uiManager->setInspectorVisible(true);
```

---

## UI 面板详解

### 1. DebugPanel - 调试信息面板

**文件**: `src/ui/panels/DebugPanel.h/.cpp`

显示实时渲染统计和调试信息。

#### 显示内容

| 类别 | 数据 |
|------|------|
| 性能 | FPS、帧时间、FPS 历史图表 |
| 渲染 | Draw Calls、三角形数、顶点数 |
| 相机 | 位置、旋转、FOV |
| 场景 | 场景名称、对象数量、渲染模式 |

#### 接口

```cpp
// 设置性能数据
void setFPS(float fps);
void setFrameTime(float ms);
void setDrawCalls(uint32_t count);
void setTriangles(uint32_t count);
void setVertices(uint32_t count);

// 设置相机数据
void setCameraPosition(const glm::vec3& pos);
void setCameraRotation(const glm::vec3& rot);
void setCameraFOV(float fov);

// 设置场景数据
void setSceneName(const std::string& name);
void setObjectCount(int count);
void setRenderMode(const std::string& mode);
```

#### 特性

- **FPS 历史图表**: 使用 `ImGui::PlotLines()` 绘制最近 120 帧的 FPS 曲线

```cpp
static constexpr int FPS_HISTORY_SIZE = 120;
float fpsHistory[FPS_HISTORY_SIZE] = {};
int fpsHistoryIndex = 0;
```

---

### 2. SceneHierarchyPanel - 场景层级面板

**文件**: `src/ui/panels/SceneHierarchyPanel.h/.cpp`

显示场景对象的树形层级结构。

#### 数据结构

```cpp
struct SceneObject {
    int id;                      // 唯一标识符
    std::string name;            // 对象名称
    std::string type;            // 类型: "Mesh", "Light", "Camera"
    bool visible = true;         // 是否可见
    std::vector<int> childrenIds; // 子对象 ID 列表
};
```

#### 接口

```cpp
// 设置场景对象列表
void setSceneObjects(const std::vector<SceneObject>& objects);

// 添加单个对象
void addObject(int id, const std::string& name, const std::string& type);

// 清空对象
void clearObjects();

// 获取选中对象
int getSelectedObjectId() const;

// 设置选中回调
void setOnSelectionChanged(std::function<void(int)> callback);
```

#### 使用示例

```cpp
auto* hierarchy = uiManager->getSceneHierarchyPanel();
if (hierarchy) {
    hierarchy->clearObjects();
    hierarchy->addObject(1, "Main Camera", "Camera");
    hierarchy->addObject(2, "Scene Root", "Node");
    hierarchy->addObject(3, "Mesh Object", "Mesh");
    hierarchy->addObject(4, "Point Light", "Light");
    
    // 监听选中变化
    hierarchy->setOnSelectionChanged([](int id) {
        std::cout << "Selected object: " << id << std::endl;
    });
}
```

---

### 3. InspectorPanel - 属性检查器面板

**文件**: `src/ui/panels/InspectorPanel.h/.cpp`

显示和编辑选中对象的属性。

#### 数据结构

```cpp
// 变换数据
struct Transform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);  // 欧拉角（度）
    glm::vec3 scale = glm::vec3(1.0f);
};

// 材质数据（PBR）
struct MaterialData {
    glm::vec3 albedo = glm::vec3(1.0f);  // 基础颜色
    float metallic = 0.0f;                // 金属度
    float roughness = 0.5f;               // 粗糙度
    float ao = 1.0f;                      // 环境光遮蔽
    bool hasAlbedoMap = false;
    bool hasNormalMap = false;
    bool hasMetallicMap = false;
    bool hasRoughnessMap = false;
};

// 光照数据
struct LightData {
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float range = 10.0f;
    int type = 0;  // 0 = Directional, 1 = Point, 2 = Spot
};
```

#### 接口

```cpp
// 设置选中对象
void setSelectedObject(int id, const std::string& name, const std::string& type);
void clearSelection();

// 变换操作
void setTransform(const Transform& transform);
const Transform& getTransform() const;

// 材质操作
void setMaterial(const MaterialData& material);
const MaterialData& getMaterial() const;

// 光照操作
void setLight(const LightData& light);

// 回调设置
void setOnTransformChanged(std::function<void(const Transform&)> callback);
void setOnMaterialChanged(std::function<void(const MaterialData&)> callback);
```

#### 面板分区

```
┌─────────────────────────────┐
│ Inspector                   │
├─────────────────────────────┤
│ ▼ Transform                 │
│   Position: [X] [Y] [Z]     │
│   Rotation: [X] [Y] [Z]     │
│   Scale:    [X] [Y] [Z]     │
├─────────────────────────────┤
│ ▼ Material (仅 Mesh 类型)    │
│   Albedo:    [■■■]          │
│   Metallic:  [━━━━○━━━━━]   │
│   Roughness: [━━━━━━○━━━]   │
│   AO:        [━━━━━━━━○━]   │
├─────────────────────────────┤
│ ▼ Light (仅 Light 类型)      │
│   Color:     [■■■]          │
│   Intensity: [━━━━○━━━━━]   │
│   Range:     [━━━━━○━━━━]   │
│   Type:      [Directional▼] │
└─────────────────────────────┘
```

---

### 4. AssetBrowserPanel - 资源浏览器面板

**文件**: `src/ui/panels/AssetBrowserPanel.h/.cpp`

浏览和管理项目资源文件。

#### 数据结构

```cpp
// 资源类型
enum class AssetType {
    Unknown,
    Texture,  // .png, .jpg, .jpeg, .tga, .bmp
    Model,    // .obj, .fbx, .gltf, .glb
    Shader,   // .vert, .frag, .glsl, .spv
    Material, // .mat, .mtl
    Scene,    // .scene
    Folder    // 目录
};

// 资源项
struct AssetItem {
    std::string name;     // 文件名
    std::string path;     // 完整路径
    AssetType type;       // 资源类型
    bool isDirectory;     // 是否为目录
};
```

#### 接口

```cpp
// 设置根目录
void setRootPath(const std::string& path);

// 刷新当前目录
void refresh();

// 回调设置
void setOnAssetDoubleClicked(std::function<void(const std::string&, AssetType)> callback);
void setOnAssetDragged(std::function<void(const std::string&, AssetType)> callback);
```

#### 功能特性

| 功能 | 描述 |
|------|------|
| 图标视图 | 使用大图标显示资源 |
| 类型图标 | 不同类型显示不同图标 |
| 搜索过滤 | 支持按名称搜索 |
| 目录导航 | 双击进入子目录 |
| 拖拽支持 | 拖拽资源到场景 |

#### 资源类型识别

```cpp
AssetType getAssetType(const std::string& extension) {
    // 纹理
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || 
        ext == ".tga" || ext == ".bmp")
        return AssetType::Texture;
    
    // 模型
    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
        return AssetType::Model;
    
    // 着色器
    if (ext == ".vert" || ext == ".frag" || ext == ".glsl" || ext == ".spv")
        return AssetType::Shader;
    
    // ...
}
```

---

## 布局系统

### 默认布局

当前项目使用**用户自由停靠**模式，首次运行时所有面板为浮动窗口。用户可以：

1. 拖拽窗口标题栏到 DockSpace 边缘停靠
2. 调整停靠区域大小
3. 将多个窗口合并为标签页

ImGui 会自动将布局保存到 `imgui.ini` 文件。

### 预设布局（可选）

如需预设布局，可使用 DockBuilder API：

```cpp
void setupDefaultDockLayout() {
    ImGuiID dockspaceId = ImGui::GetID("VEngineDockSpace");
    
    // 检查是否首次运行
    if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);
        
        // 分割节点
        ImGuiID leftId, rightId, bottomId, centralId;
        ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.18f, 
                                     &leftId, &centralId);
        ImGui::DockBuilderSplitNode(centralId, ImGuiDir_Right, 0.22f, 
                                     &rightId, &centralId);
        ImGui::DockBuilderSplitNode(centralId, ImGuiDir_Down, 0.25f, 
                                     &bottomId, &centralId);
        
        // 停靠窗口
        ImGui::DockBuilderDockWindow("Scene Hierarchy", leftId);
        ImGui::DockBuilderDockWindow("Inspector", rightId);
        ImGui::DockBuilderDockWindow("Asset Browser", bottomId);
        ImGui::DockBuilderDockWindow("Debug Info", leftId);
        
        ImGui::DockBuilderFinish(dockspaceId);
    }
}
```

布局效果：

```
┌─────────────────────────────────────────────────────────────────┐
│ File  Edit  View  Help                                          │
├──────────────┬─────────────────────────────┬────────────────────┤
│ Scene        │                             │ Inspector          │
│ Hierarchy    │                             │                    │
│ ├─ Camera    │       3D Viewport           │ ▼ Transform        │
│ ├─ Light     │       (Central Node)        │   Position: ...    │
│ └─ Mesh      │                             │   Rotation: ...    │
│──────────────│                             │                    │
│ Debug Info   │                             │ ▼ Material         │
│ FPS: 60      │                             │   Albedo: ...      │
│ Triangles:   │                             │   Roughness: ...   │
├──────────────┴─────────────────────────────┴────────────────────┤
│ Asset Browser                                                    │
│ [📁 Models] [🖼️ Textures] [📄 Shaders]                           │
└──────────────────────────────────────────────────────────────────┘
```

---

## 集成指南

### 添加新面板

1. **创建面板类**

```cpp
// src/ui/panels/MyPanel.h
#pragma once

class MyPanel {
public:
    MyPanel() = default;
    ~MyPanel() = default;
    
    void render();
    
    // 添加数据设置方法...
};

// src/ui/panels/MyPanel.cpp
#include "MyPanel.h"
#include <imgui.h>

void MyPanel::render() {
    ImGui::Begin("My Panel");
    
    // 绘制 UI 内容...
    ImGui::Text("Hello, World!");
    
    ImGui::End();
}
```

2. **注册到 UIManager**

```cpp
// UIManager.h
#include "panels/MyPanel.h"

class UIManager {
    // ...
private:
    std::unique_ptr<MyPanel> myPanel;
public:
    MyPanel* getMyPanel() { return myPanel.get(); }
};

// UIManager.cpp
UIManager::UIManager() {
    // ...
    myPanel = std::make_unique<MyPanel>();
}

void UIManager::render() {
    // ...
    if (myPanel) myPanel->render();
}
```

### 输入处理

ImGui 会自动处理鼠标和键盘输入。使用以下方法判断是否应该传递给 3D 场景：

```cpp
// 检查 ImGui 是否需要鼠标输入
if (!imguiLayer->wantCaptureMouse()) {
    // 处理 3D 场景的鼠标输入
    handleSceneMouseInput();
}

// 检查 ImGui 是否需要键盘输入
if (!imguiLayer->wantCaptureKeyboard()) {
    // 处理 3D 场景的键盘输入
    handleSceneKeyboardInput();
}
```

---

## 样式自定义

ImGuiLayer 中的 `setupStyle()` 方法定义了 UI 主题：

```cpp
void ImGuiLayer::setupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // 窗口
    style.WindowRounding = 4.0f;
    style.WindowPadding = ImVec2(8, 8);
    
    // 控件
    style.FrameRounding = 3.0f;
    style.FramePadding = ImVec2(6, 4);
    
    // 颜色
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.12f, 0.95f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.2f, 0.22f, 1.0f);
    // ...
}
```

---

## 常见问题

### Q: 窗口大小改变后 UI 崩溃？

**A**: 确保在 `recreateSwapChain()` 中调用 `imguiLayer->onResize()`：

```cpp
void VulkanRenderer::recreateSwapChain() {
    // ... 重建交换链 ...
    
    if (imguiLayer) {
        imguiLayer->onResize(width, height, swapChain->getRenderPass());
    }
}
```

### Q: 如何让中央区域显示 3D 场景？

**A**: 使用 `ImGuiDockNodeFlags_PassthruCentralNode` 标志，该标志使中央节点透明，可以看到下面渲染的 3D 内容。

### Q: 面板位置没有保存？

**A**: ImGui 默认将布局保存到 `imgui.ini`。确保：
1. 程序有写入权限
2. 没有禁用 `io.IniFilename`

### Q: 如何实现面板间通信？

**A**: 通过 UIManager 或回调函数：

```cpp
// 方式 1: 通过 UIManager
auto* hierarchy = uiManager->getSceneHierarchyPanel();
auto* inspector = uiManager->getInspectorPanel();
int selectedId = hierarchy->getSelectedObjectId();
inspector->setSelectedObject(selectedId, ...);

// 方式 2: 回调函数
hierarchy->setOnSelectionChanged([this](int id) {
    inspector->setSelectedObject(id, ...);
});
```

---

## 参考资源

- [Dear ImGui 官方仓库](https://github.com/ocornut/imgui)
- [ImGui Docking 分支文档](https://github.com/ocornut/imgui/tree/docking)
- [ImGui Vulkan 后端示例](https://github.com/ocornut/imgui/blob/docking/examples/example_glfw_vulkan/main.cpp)
