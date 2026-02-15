#pragma once

#include <entt/entt.hpp>
#include "Scene.h"
#include "Components.h"

namespace VulkanEngine {

/**
 * @brief 实体类 - 对 entt::entity 的封装
 * 
 * Entity 是一个轻量级的句柄类，它封装了 entt::entity 和对应的 Scene 指针。
 * 提供了方便的组件操作接口。
 */
class Entity {
public:
    Entity() = default;
    Entity(entt::entity handle, Scene* scene);
    Entity(const Entity& other) = default;

    // ============================================================
    // 组件操作
    // ============================================================

    /**
     * @brief 添加组件
     * @tparam T 组件类型
     * @tparam Args 构造参数类型
     * @param args 组件构造参数
     * @return 添加的组件引用
     */
    template<typename T, typename... Args>
    T& addComponent(Args&&... args) {
        assert(!hasComponent<T>() && "Entity already has component!");
        return m_scene->m_registry.emplace<T>(m_entityHandle, std::forward<Args>(args)...);
    }

    /**
     * @brief 添加或替换组件
     * @tparam T 组件类型
     * @tparam Args 构造参数类型
     * @param args 组件构造参数
     * @return 组件引用
     */
    template<typename T, typename... Args>
    T& addOrReplaceComponent(Args&&... args) {
        return m_scene->m_registry.emplace_or_replace<T>(m_entityHandle, std::forward<Args>(args)...);
    }

    /**
     * @brief 获取组件
     * @tparam T 组件类型
     * @return 组件引用
     */
    template<typename T>
    T& getComponent() {
        assert(hasComponent<T>() && "Entity does not have component!");
        return m_scene->m_registry.get<T>(m_entityHandle);
    }

    /**
     * @brief 获取组件 (const 版本)
     * @tparam T 组件类型
     * @return 组件常引用
     */
    template<typename T>
    const T& getComponent() const {
        assert(hasComponent<T>() && "Entity does not have component!");
        return m_scene->m_registry.get<T>(m_entityHandle);
    }

    /**
     * @brief 尝试获取组件
     * @tparam T 组件类型
     * @return 组件指针，如果没有返回 nullptr
     */
    template<typename T>
    T* tryGetComponent() {
        return m_scene->m_registry.try_get<T>(m_entityHandle);
    }

    /**
     * @brief 尝试获取组件 (const 版本)
     * @tparam T 组件类型
     * @return 组件常指针，如果没有返回 nullptr
     */
    template<typename T>
    const T* tryGetComponent() const {
        return m_scene->m_registry.try_get<T>(m_entityHandle);
    }

    /**
     * @brief 检查是否拥有组件
     * @tparam T 组件类型
     * @return 是否拥有该组件
     */
    template<typename T>
    bool hasComponent() const {
        return m_scene->m_registry.all_of<T>(m_entityHandle);
    }

    /**
     * @brief 检查是否拥有所有指定组件
     * @tparam T 组件类型列表
     * @return 是否拥有所有组件
     */
    template<typename... T>
    bool hasComponents() const {
        return m_scene->m_registry.all_of<T...>(m_entityHandle);
    }

    /**
     * @brief 检查是否拥有任一指定组件
     * @tparam T 组件类型列表
     * @return 是否拥有任一组件
     */
    template<typename... T>
    bool hasAnyComponent() const {
        return m_scene->m_registry.any_of<T...>(m_entityHandle);
    }

    /**
     * @brief 移除组件
     * @tparam T 组件类型
     */
    template<typename T>
    void removeComponent() {
        assert(hasComponent<T>() && "Entity does not have component!");
        m_scene->m_registry.remove<T>(m_entityHandle);
    }

    // ============================================================
    // 便捷访问器
    // ============================================================

    /**
     * @brief 获取实体名称
     */
    const std::string& getName() const {
        return getComponent<TagComponent>().tag;
    }

    /**
     * @brief 设置实体名称
     */
    void setName(const std::string& name) {
        getComponent<TagComponent>().tag = name;
    }

    /**
     * @brief 获取变换组件
     */
    TransformComponent& getTransform() {
        return getComponent<TransformComponent>();
    }

    /**
     * @brief 获取变换组件 (const)
     */
    const TransformComponent& getTransform() const {
        return getComponent<TransformComponent>();
    }

    /**
     * @brief 获取 UUID
     */
    uint64_t getUUID() const {
        if (hasComponent<UUIDComponent>()) {
            return getComponent<UUIDComponent>().uuid;
        }
        return 0;
    }

    // ============================================================
    // 层级关系
    // ============================================================

    /**
     * @brief 设置父实体
     */
    void setParent(Entity parent);

    /**
     * @brief 获取父实体
     */
    Entity getParent() const;

    /**
     * @brief 获取子实体列表
     */
    std::vector<Entity> getChildren() const;

    /**
     * @brief 添加子实体
     */
    void addChild(Entity child);

    /**
     * @brief 移除子实体
     */
    void removeChild(Entity child);

    /**
     * @brief 是否有父实体
     */
    bool hasParent() const;

    /**
     * @brief 是否有子实体
     */
    bool hasChildren() const;

    // ============================================================
    // 操作符重载
    // ============================================================

    operator bool() const { return m_entityHandle != entt::null && m_scene != nullptr; }
    operator entt::entity() const { return m_entityHandle; }
    operator uint32_t() const { return static_cast<uint32_t>(m_entityHandle); }

    bool operator==(const Entity& other) const {
        return m_entityHandle == other.m_entityHandle && m_scene == other.m_scene;
    }

    bool operator!=(const Entity& other) const {
        return !(*this == other);
    }

    // ============================================================
    // 访问器
    // ============================================================

    entt::entity getHandle() const { return m_entityHandle; }
    Scene* getScene() const { return m_scene; }

private:
    entt::entity m_entityHandle = entt::null;
    Scene* m_scene = nullptr;
};

} // namespace VulkanEngine
