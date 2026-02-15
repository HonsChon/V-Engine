#include "Scene.h"
#include "Entity.h"
#include "Components.h"

#include <random>
#include <chrono>

namespace VulkanEngine {

Scene::Scene(const std::string& name)
    : m_name(name) {
    // 使用当前时间作为随机数种子，用于 UUID 生成
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    m_nextUUID = static_cast<uint64_t>(seed) & 0xFFFFFFFF;
}

Scene::~Scene() {
    // EnTT registry 会自动清理所有实体和组件
    m_registry.clear();
}

// ============================================================
// 实体管理
// ============================================================

Entity Scene::createEntity(const std::string& name) {
    return createEntityWithUUID(generateUUID(), name);
}

Entity Scene::createEntityWithUUID(uint64_t uuid, const std::string& name) {
    Entity entity(m_registry.create(), this);
    
    // 每个实体都必须有 TagComponent 和 TransformComponent
    entity.addComponent<UUIDComponent>(uuid);
    entity.addComponent<TagComponent>(name);
    entity.addComponent<TransformComponent>();
    
    return entity;
}

void Scene::destroyEntity(Entity entity) {
    if (!entity) return;
    
    // 如果有父子关系，需要先处理
    if (entity.hasComponent<RelationshipComponent>()) {
        auto& relationship = entity.getComponent<RelationshipComponent>();
        
        // 移除与父节点的关系
        if (relationship.parent != entt::null) {
            Entity parent(relationship.parent, this);
            parent.removeChild(entity);
        }
        
        // 递归销毁所有子节点
        auto children = entity.getChildren();
        for (auto& child : children) {
            destroyEntity(child);
        }
    }
    
    m_registry.destroy(entity.getHandle());
}

Entity Scene::duplicateEntity(Entity entity) {
    if (!entity) return Entity();
    
    std::string name = entity.getName() + " (Copy)";
    Entity newEntity = createEntity(name);
    
    // 复制 TransformComponent
    if (entity.hasComponent<TransformComponent>()) {
        newEntity.getComponent<TransformComponent>() = entity.getComponent<TransformComponent>();
    }
    
    // 复制 MeshRendererComponent
    if (entity.hasComponent<MeshRendererComponent>()) {
        newEntity.addComponent<MeshRendererComponent>(entity.getComponent<MeshRendererComponent>());
    }
    
    // 复制 CameraComponent
    if (entity.hasComponent<CameraComponent>()) {
        auto camera = entity.getComponent<CameraComponent>();
        camera.isPrimary = false;  // 复制的相机不是主相机
        newEntity.addComponent<CameraComponent>(camera);
    }
    
    // 复制 LightComponent
    if (entity.hasComponent<LightComponent>()) {
        newEntity.addComponent<LightComponent>(entity.getComponent<LightComponent>());
    }
    
    // 复制 PBRMaterialComponent
    if (entity.hasComponent<PBRMaterialComponent>()) {
        newEntity.addComponent<PBRMaterialComponent>(entity.getComponent<PBRMaterialComponent>());
    }
    
    return newEntity;
}

Entity Scene::findEntityByName(const std::string& name) {
    auto view = m_registry.view<TagComponent>();
    for (auto entity : view) {
        const auto& tag = view.get<TagComponent>(entity);
        if (tag.tag == name) {
            return Entity(entity, this);
        }
    }
    return Entity();
}

Entity Scene::findEntityByUUID(uint64_t uuid) {
    auto view = m_registry.view<UUIDComponent>();
    for (auto entity : view) {
        const auto& uuidComp = view.get<UUIDComponent>(entity);
        if (uuidComp.uuid == uuid) {
            return Entity(entity, this);
        }
    }
    return Entity();
}

std::vector<Entity> Scene::getAllEntities() {
    std::vector<Entity> entities;
    auto view = m_registry.view<TagComponent>();
    for (auto entity : view) {
        entities.emplace_back(entity, this);
    }
    return entities;
}

std::vector<Entity> Scene::getRootEntities() {
    std::vector<Entity> roots;
    auto view = m_registry.view<TagComponent>();
    
    for (auto entityHandle : view) {
        Entity entity(entityHandle, this);
        
        // 如果没有 RelationshipComponent，或者 parent 是 null，则是根节点
        if (!entity.hasComponent<RelationshipComponent>()) {
            roots.push_back(entity);
        }
        else {
            auto& relationship = entity.getComponent<RelationshipComponent>();
            if (relationship.parent == entt::null) {
                roots.push_back(entity);
            }
        }
    }
    
    return roots;
}

// ============================================================
// 场景生命周期
// ============================================================

void Scene::onStart() {
    m_isRunning = true;
    m_isPaused = false;
    
    // 初始化所有脚本组件
    auto view = m_registry.view<NativeScriptComponent>();
    for (auto entity : view) {
        auto& script = view.get<NativeScriptComponent>(entity);
        if (script.instantiateScript && !script.instance) {
            script.instantiateScript(script);
        }
    }
}

void Scene::onUpdate(float deltaTime) {
    if (!m_isRunning || m_isPaused) return;
    
    // 更新所有脚本组件
    {
        auto view = m_registry.view<NativeScriptComponent>();
        for (auto entity : view) {
            auto& script = view.get<NativeScriptComponent>(entity);
            // 如果有实例，可以调用其 OnUpdate 方法
            // 这里需要定义 ScriptableEntity 基类
        }
    }
    
    // 更新相机宽高比
    {
        auto view = m_registry.view<CameraComponent>();
        for (auto entity : view) {
            auto& camera = view.get<CameraComponent>(entity);
            if (!camera.fixedAspectRatio && m_viewportWidth > 0 && m_viewportHeight > 0) {
                camera.aspectRatio = static_cast<float>(m_viewportWidth) / static_cast<float>(m_viewportHeight);
            }
        }
    }
}

void Scene::onStop() {
    m_isRunning = false;
    
    // 销毁所有脚本实例
    auto view = m_registry.view<NativeScriptComponent>();
    for (auto entity : view) {
        auto& script = view.get<NativeScriptComponent>(entity);
        if (script.destroyScript && script.instance) {
            script.destroyScript(script);
        }
    }
}

void Scene::onViewportResize(uint32_t width, uint32_t height) {
    m_viewportWidth = width;
    m_viewportHeight = height;
    
    // 更新所有非固定宽高比的相机
    auto view = m_registry.view<CameraComponent>();
    for (auto entity : view) {
        auto& camera = view.get<CameraComponent>(entity);
        if (!camera.fixedAspectRatio) {
            camera.aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        }
    }
}

// ============================================================
// 主相机
// ============================================================

Entity Scene::getPrimaryCameraEntity() {
    auto view = m_registry.view<CameraComponent>();
    for (auto entity : view) {
        const auto& camera = view.get<CameraComponent>(entity);
        if (camera.isPrimary) {
            return Entity(entity, this);
        }
    }
    return Entity();
}

// ============================================================
// 私有方法
// ============================================================

uint64_t Scene::generateUUID() {
    // 简单的 UUID 生成器
    // 在生产环境中，应该使用更可靠的 UUID 生成方案
    return m_nextUUID++;
}

} // namespace VulkanEngine
