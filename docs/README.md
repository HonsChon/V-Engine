# V Engine 文档中心

## 概述

**V Engine** 是一个基于 Vulkan 的现代 PBR 渲染引擎，采用模块化架构设计，参考 Unreal Engine 的设计理念。

---

## 📁 文档目录

### 架构设计

| 文档 | 描述 |
|------|------|
| [架构概览](#架构概览) | 引擎整体架构设计 |
| [Core README](core/README.md) | Vulkan 核心封装和基础设施 |

### 系统文档

| 文档 | 描述 |
|------|------|
| [Scene Management](Scene-Management-System.md) | ECS 场景管理系统设计 |
| [UI System](UI-System.md) | ImGui 集成与 UI 系统 |

### GPU 驱动渲染

| 文档 | 描述 |
|------|------|
| [GPU-Driven Rendering](gpu-driven-rendering/README.md) | GPU 驱动渲染管线概述 |
| [**Nanite 虚拟几何**](nanite/README.md) | 类 UE5 的虚拟几何系统 |

### 渲染效果

| 文档 | 描述 |
|------|------|
| [SSR & Water Rendering](SSR_Water_Rendering.md) | 屏幕空间反射与水体渲染 |

---

## 🏛️ 架构概览

### v1.0.0 新架构 (2026-03)

V Engine v1.0.0 进行了重大架构重构，参考 Unreal Engine 的模块化设计：

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              Application Layer                           │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                           Engine                                 │    │
│  │  • 应用程序生命周期管理                                           │    │
│  │  • 子系统初始化/销毁                                              │    │
│  │  • 主循环控制                                                     │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐                    │
│  │ Window  │  │  Input  │  │ Camera  │  │  Scene  │                    │
│  └─────────┘  └─────────┘  └─────────┘  └─────────┘                    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                              Renderer Layer                              │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                        SceneRenderer                             │    │
│  │  • 渲染通道调度器                                                 │    │
│  │  • 管理多个 RenderPass                                            │    │
│  │  • 协调渲染顺序                                                   │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                       RenderPassBase                              │   │
│  │  ┌───────────┐ ┌───────────┐ ┌───────────┐ ┌──────────┐          │   │
│  │  │ GBuffer   │ │ Lighting  │ │  Forward  │ │  Water   │ ...      │   │
│  │  │   Pass    │ │   Pass    │ │   Pass    │ │   Pass   │          │   │
│  │  └───────────┘ └───────────┘ └───────────┘ └──────────┘          │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                         RenderSystem                             │    │
│  │  • ECS 系统组件                                                   │    │
│  │  • 收集可渲染实体                                                  │    │
│  │  • 分配到各个 Pass                                                │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                                RHI Layer                                 │
│  (Render Hardware Interface - Vulkan 抽象层)                             │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌─────────────────────┐   │
│  │  Vulkan   │  │  Vulkan   │  │   Frame   │  │   VulkanBuffer/     │   │
│  │  Device   │  │ SwapChain │  │ Resources │  │   VulkanTexture     │   │
│  └───────────┘  └───────────┘  └───────────┘  └─────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                              ECS / Scene                                 │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  Scene (EnTT Registry)                                            │  │
│  │  ┌─────────────────────────────────────────────────────────────┐  │  │
│  │  │  Entity                                                     │  │  │
│  │  │  ├── TransformComponent                                     │  │  │
│  │  │  ├── MeshRendererComponent                                  │  │  │
│  │  │  ├── PBRMaterialComponent                                   │  │  │
│  │  │  ├── LightComponent                                         │  │  │
│  │  │  └── TagComponent                                           │  │  │
│  │  └─────────────────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

### 模块职责

| 模块 | 职责 | 主要类 |
|------|------|--------|
| **Application** | 应用程序生命周期 | `Engine`, `Window`, `Input` |
| **RHI** | Vulkan 资源管理 | `VulkanDevice`, `VulkanSwapChain`, `FrameResources` |
| **Renderer** | 渲染通道调度 | `SceneRenderer`, `RenderPassBase`, `ForwardPass` |
| **Scene** | ECS 场景管理 | `Scene`, `Entity`, `Components` |
| **Resources** | 资源加载缓存 | `MeshManager`, `TextureManager`, `Material` |
| **UI** | 编辑器界面 | `ImGuiLayer`, `UIManager`, `Panels` |

### 关键数据结构

#### RenderContext
渲染上下文，在所有渲染通道间共享：

```cpp
struct RenderContext {
    // 命令缓冲
    VkCommandBuffer commandBuffer;
    uint32_t frameIndex;
    uint32_t imageIndex;
    
    // 相机数据
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::mat4 invViewMatrix;
    glm::mat4 invProjectionMatrix;
    glm::vec3 cameraPosition;
    
    // 时间数据
    float deltaTime;
    float time;
    
    // 屏幕尺寸
    VkExtent2D extent;
    uint32_t screenWidth;
    uint32_t screenHeight;
    
    // 场景引用
    Camera* camera;
    Scene* scene;
};
```

#### RenderPassBase
渲染通道基类接口：

```cpp
class RenderPassBase {
public:
    virtual ~RenderPassBase() = default;
    
    virtual void initialize() = 0;
    virtual void render(const RenderContext& context) = 0;
    virtual void resize(uint32_t width, uint32_t height);
    
    void setEnabled(bool enable);
    bool isEnabled() const;
    
protected:
    bool m_enabled = true;
};
```

---

## � 项目进度总览

### 架构重构
```
Phase 1: 模块拆分设计        [██████████] 100%
Phase 2: RHI 层实现          [██████████] 100%
Phase 3: SceneRenderer       [██████████] 100%
Phase 4: ECS 集成            [██████████] 100%
Phase 5: UI 系统迁移         [██████████] 100%
```

### GPU-Driven / Nanite 系统

```
Phase 1: GPU 剔除基础         [██████████] 100%
Phase 2: Mesh Clustering      [██████████] 100%
Phase 3: 动态 LOD             [██████████] 100%
Phase 4: 网格简化             [░░░░░░░░░░]   0%
Phase 5: Visibility Buffer    [░░░░░░░░░░]   0%
```

### 已完成功能

- ✅ **v1.0.0 模块化架构重构**
  - Engine 应用程序层
  - RHI 渲染硬件抽象层
  - SceneRenderer 渲染调度器
  - RenderContext 渲染上下文
  - ECS 系统完善
- ✅ Vulkan 核心封装（Device, Buffer, Pipeline, Descriptor）
- ✅ PBR 材质系统（GGX BRDF）
- ✅ 延迟渲染管线（G-Buffer, Deferred Lighting）
- ✅ 屏幕空间反射（SSR）
- ✅ 水面渲染系统
- ✅ GPU 视锥剔除（Compute Shader）
- ✅ **Nanite Mesh Clustering（METIS 多级图分区 + 16-bit 量化）**
- ✅ **动态 LOD 选择系统**

### 进行中/计划中

- 🔲 网格简化（Edge Collapse）
- 🔲 屏幕空间 LOD 选择
- 🔲 Visibility Buffer 渲染
- 🔲 HZB 遮挡剔除
- 🔲 多光源支持
- 🔲 阴影系统 (CSM)

---

## 🔧 快速开始

### 构建项目

```bash
# 配置
cmake -B build -S . -G "Visual Studio 17 2022"

# 构建
cmake --build build --config Release

# 运行
./build/bin/VulkanPBR.exe
```

### 运行时快捷键

| 按键 | 功能 |
|------|------|
| `WASD` | 相机移动 |
| `鼠标右键` | 相机旋转 |
| `1-4` | 切换几何体 |
| `5` | 切换水面场景 |
| `7` | 切换 Nanite 开/关 |
| `8` | 测试 Mesh Clustering |
| `F1` | 显示/隐藏 UI |
| `ESC` | 退出程序 |

---

## �📚 技术参考

### 引擎架构
- [Game Engine Architecture (Jason Gregory)](https://www.gameenginebook.com/)
- [Unreal Engine Source](https://github.com/EpicGames/UnrealEngine)
- [The Cherno - Game Engine Series](https://www.youtube.com/playlist?list=PLlrATfBNZ98dC-V-N3m0Go4deliWHPFwT)

### 渲染技术
- [Vulkan Guide](https://vkguide.dev/)
- [Learn OpenGL - PBR](https://learnopengl.com/PBR/Theory)
- [Vulkan Tutorial](https://vulkan-tutorial.com/)

### GPU-Driven / Nanite
- [A Deep Dive into Nanite (SIGGRAPH 2021)](https://advances.realtimerendering.com/s2021/)
- [GPU-Driven Rendering Pipelines (SIGGRAPH 2015)](http://advances.realtimerendering.com/s2015/)
- [Real Shading in Unreal Engine 4 (SIGGRAPH 2013)](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf)

---

## 📝 更新日志

### v1.0.0 (2026-03-10) - 模块化架构重构
- 新增：参考 Unreal Engine 的模块化架构设计
  - `Engine` 应用程序层 - 生命周期管理
  - `RHI` 层 - Vulkan 资源抽象
  - `SceneRenderer` - 渲染通道调度
  - `RenderContext` - 统一渲染上下文
  - `RenderPassBase` - 渲染通道基类
- 新增：完善的 ECS 系统
  - `Scene` 管理 EnTT Registry
  - `Entity` 友好封装
  - `RenderSystem` 渲染系统组件
- 新增：`FrameResources` 帧资源管理
- 重构：目录结构按功能模块划分

### v0.9.3 (2026-03-09) - Nanite Phase 2
- 新增：动态 LOD 选择系统
  - 距离驱动的 LOD 计算
  - DAG 层级结构
  - 互斥渲染

### v0.9.2 (2026-03-08) - Nanite Phase 1
- 新增：Nanite Mesh Clustering 系统
  - METIS 多级图分区算法
  - 16-bit 顶点量化
  - 法线锥背面剔除
  - NaniteManager GPU 缓冲管理

### v0.9.1 - GPU Culling & SSR
- 新增：GPU 视锥剔除
- 新增：屏幕空间反射
- 新增：水面渲染系统

### v0.9.0 - Initial
- 基础 PBR 渲染
- Vulkan 核心封装
- ImGui 编辑器 UI