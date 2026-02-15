# V Engine 场景管理系统设计文档

## 1. 概述

场景管理系统是游戏引擎的核心模块，负责管理游戏世界中的所有对象及其关系。本文档描述 V Engine 场景管理系统的完整设计方案。

## 2. 架构选型

### 2.1 ECS vs 传统继承

| 特性 | ECS 架构 | 传统继承 |
|------|----------|----------|
| 性能 | 高（缓存友好） | 中等 |
| 灵活性 | 极高 | 中等 |
| 复杂度 | 较高 | 较低 |
| 适用场景 | 大量实体 | 少量实体 |

**选定方案**: 使用 **EnTT** 库实现 ECS 架构

### 2.2 系统架构图

```
┌─────────────────────────────────────────────────────────────┐
│                      SceneManager                            │
│  • 管理多场景                                                │
│  • 场景切换                                                  │
│  • 全局系统协调                                              │
└─────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
        ┌──────────┐    ┌──────────┐    ┌──────────┐
        │  Scene   │    │  Scene   │    │  Scene   │
        │ "Main"   │    │ "Menu"   │    │ "Level1" │
        └──────────┘    └──────────┘    └──────────┘
              │
              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Entity Registry                           │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐           │
│  │Entity 1 │ │Entity 2 │ │Entity 3 │ │Entity N │           │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘           │
└─────────────────────────────────────────────────────────────┘
              │
              ▼
┌─────────────────────────────────────────────────────────────┐
│                   Component Pools                            │
│  ┌───────────┐ ┌───────────┐ ┌───────────┐ ┌────────────┐  │
│  │ Transform │ │   Mesh    │ │   Light   │ │   Camera   │  │
│  │   Pool    │ │   Pool    │ │   Pool    │ │    Pool    │  │
│  └───────────┘ └───────────┘ └───────────┘ └────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## 3. 文件结构

```
src/scene/
├── Components.h         # 所有组件定义
├── Entity.h/.cpp        # 实体封装类
├── Scene.h/.cpp         # 场景类
└── SceneManager.h/.cpp  # 场景管理器
```

## 4. 核心类设计

### 4.1 Scene 类

场景是实体和组件的容器，持有一个 `entt::registry`。

```cpp
class Scene {
public:
    // 实体管理
    Entity createEntity(const std::string& name);
    Entity createEntityWithUUID(uint64_t uuid, const std::string& name);
    void destroyEntity(Entity entity);
    Entity duplicateEntity(Entity entity);
    
    // 查找
    Entity findEntityByName(const std::string& name);
    Entity findEntityByUUID(uint64_t uuid);
    std::vector<Entity> getAllEntities();
    std::vector<Entity> getRootEntities();
    
    // 生命周期
    void onStart();
    void onUpdate(float deltaTime);
    void onStop();
    void onViewportResize(uint32_t width, uint32_t height);
    
    // 主相机
    Entity getPrimaryCameraEntity();
    
private:
    std::string m_name;
    entt::registry m_registry;
};
```

### 4.2 Entity 类

Entity 是对 `entt::entity` 的轻量级封装，提供方便的组件操作接口。

```cpp
class Entity {
public:
    // 组件操作
    template<typename T, typename... Args>
    T& addComponent(Args&&... args);
    
    template<typename T>
    T& getComponent();
    
    template<typename T>
    bool hasComponent() const;
    
    template<typename T>
    void removeComponent();
    
    // 层级关系
    void setParent(Entity parent);
    Entity getParent() const;
    std::vector<Entity> getChildren() const;
    
private:
    entt::entity m_entityHandle;
    Scene* m_scene;
};
```

### 4.3 SceneManager 类

单例模式，管理多个场景的创建、切换和销毁。

```cpp
class SceneManager {
public:
    static SceneManager& getInstance();
    
    // 场景管理
    std::shared_ptr<Scene> createScene(const std::string& name);
    void setActiveScene(std::shared_ptr<Scene> scene);
    std::shared_ptr<Scene> getActiveScene() const;
    void unloadScene(const std::string& name);
    
    // 场景切换
    void switchToScene(const std::string& name, bool async = false);
    
    // 生命周期
    void update(float deltaTime);
    void onViewportResize(uint32_t width, uint32_t height);
};
```

## 5. 组件设计

### 5.1 核心组件

| 组件 | 描述 |
|------|------|
| `TagComponent` | 实体名称标识 |
| `TransformComponent` | 位置、旋转、缩放 |
| `UUIDComponent` | 唯一标识符 |
| `RelationshipComponent` | 父子层级关系 |

### 5.2 渲染组件

| 组件 | 描述 |
|------|------|
| `MeshRendererComponent` | 网格渲染器 |
| `PBRMaterialComponent` | PBR 材质属性 |
| `CameraComponent` | 相机 |
| `LightComponent` | 光源 |

### 5.3 物理组件（预留）

| 组件 | 描述 |
|------|------|
| `RigidBodyComponent` | 刚体 |
| `BoxColliderComponent` | 盒形碰撞体 |
| `SphereColliderComponent` | 球形碰撞体 |

### 5.4 脚本组件（预留）

| 组件 | 描述 |
|------|------|
| `NativeScriptComponent` | C++ 脚本绑定 |

## 6. TransformComponent 详解

```cpp
struct TransformComponent {
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };  // 欧拉角 (弧度)
    glm::vec3 scale = { 1.0f, 1.0f, 1.0f };

    // 获取模型矩阵
    glm::mat4 getTransform() const {
        glm::mat4 rotationMatrix = glm::toMat4(glm::quat(rotation));
        return glm::translate(glm::mat4(1.0f), position)
            * rotationMatrix
            * glm::scale(glm::mat4(1.0f), scale);
    }
    
    // 获取方向向量
    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;
};
```

## 7. 层级关系系统

使用链表结构实现父子关系：

```cpp
struct RelationshipComponent {
    entt::entity parent = entt::null;
    entt::entity firstChild = entt::null;
    entt::entity nextSibling = entt::null;
    entt::entity prevSibling = entt::null;
    size_t childrenCount = 0;
};
```

### 7.1 层级关系图示

```
      Parent
         │
         ▼
   firstChild ──► nextSibling ──► nextSibling
         │              │              │
         ▼              ▼              ▼
      Child1        Child2         Child3
```

## 8. 使用示例

### 8.1 创建场景和实体

```cpp
// 获取场景管理器
auto& sceneManager = SceneManager::getInstance();

// 创建场景
auto scene = sceneManager.createScene("MainScene");

// 创建实体
auto cube = scene->createEntity("Cube");
cube.addComponent<MeshRendererComponent>("assets/models/cube.obj");

auto& transform = cube.getTransform();
transform.position = { 0.0f, 1.0f, 0.0f };
transform.scale = { 2.0f, 2.0f, 2.0f };
```

### 8.2 创建相机

```cpp
auto cameraEntity = scene->createEntity("Main Camera");
auto& camera = cameraEntity.addComponent<CameraComponent>();
camera.isPrimary = true;
camera.fov = glm::radians(60.0f);

auto& cameraTransform = cameraEntity.getTransform();
cameraTransform.position = { 0.0f, 5.0f, 10.0f };
```

### 8.3 创建光源

```cpp
auto lightEntity = scene->createEntity("Directional Light");
auto& light = lightEntity.addComponent<LightComponent>(LightType::Directional);
light.color = { 1.0f, 0.95f, 0.9f };
light.intensity = 1.0f;

auto& lightTransform = lightEntity.getTransform();
lightTransform.rotation = { glm::radians(-45.0f), glm::radians(45.0f), 0.0f };
```

### 8.4 父子关系

```cpp
auto parent = scene->createEntity("Parent");
auto child1 = scene->createEntity("Child1");
auto child2 = scene->createEntity("Child2");

child1.setParent(parent);
child2.setParent(parent);

// 或者
parent.addChild(child1);
parent.addChild(child2);
```

### 8.5 遍历组件

```cpp
// 遍历所有带有 MeshRendererComponent 的实体
auto view = scene->getRegistry().view<TransformComponent, MeshRendererComponent>();
for (auto entity : view) {
    auto& transform = view.get<TransformComponent>(entity);
    auto& mesh = view.get<MeshRendererComponent>(entity);
    
    // 渲染...
}
```

## 9. 与渲染器集成

### 9.1 从 ECS 提取渲染数据

```cpp
void RenderSystem::render(Scene& scene, VulkanRenderer& renderer) {
    auto cameraEntity = scene.getPrimaryCameraEntity();
    if (!cameraEntity) return;
    
    auto& cameraTransform = cameraEntity.getTransform();
    auto& camera = cameraEntity.getComponent<CameraComponent>();
    
    glm::mat4 view = glm::inverse(cameraTransform.getTransform());
    glm::mat4 projection = camera.getProjection();
    
    // 渲染所有网格
    auto view = scene.getRegistry().view<TransformComponent, MeshRendererComponent>();
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& mesh = view.get<MeshRendererComponent>(entity);
        
        if (!mesh.visible) continue;
        
        glm::mat4 model = transform.getTransform();
        renderer.drawMesh(mesh.meshPath, model, view, projection);
    }
}
```

## 10. 后续扩展计划

### 10.1 场景序列化
- 支持 JSON/YAML 格式保存和加载场景
- 实现 SceneSerializer 类

### 10.2 预制体系统
- 支持将实体及其组件保存为预制体
- 支持预制体实例化

### 10.3 脚本系统
- 实现 ScriptableEntity 基类
- 支持 C++ 脚本的热重载

### 10.4 空间分区
- 实现 BVH (Bounding Volume Hierarchy)
- 实现视锥体剔除

## 11. 依赖

- **EnTT** v3.16.0 - ECS 库
- **GLM** - 数学库

## 12. 参考资料

- [EnTT Wiki](https://github.com/skypjack/entt/wiki)
- [Game Programming Patterns - Component](https://gameprogrammingpatterns.com/component.html)
- [ECS Back and Forth](https://skypjack.github.io/2019-02-14-ecs-baf-part-1/)