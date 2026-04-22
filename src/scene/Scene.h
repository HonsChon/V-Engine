#pragma once

#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace VulkanEngine {

// 前向声明
class Entity;

/**
 * @brief 场景类 - 管理实体和组件的容器
 * 
 * Scene 是 ECS 架构的核心，它持有一个 entt::registry，
 * 所有实体和组件都存储在这个 registry 中。
 */
class Scene {
public:
    Scene(const std::string& name = "Untitled");
    ~Scene();

    // 禁止复制
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // 允许移动
    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;

    // ============================================================
    // 实体管理
    // ============================================================
    
    /**
     * @brief 创建一个新实体
     * @param name 实体名称
     * @return 新创建的实体
     */
    Entity createEntity(const std::string& name = "Entity");

    /**
     * @brief 创建带有 UUID 的实体
     * @param uuid 唯一标识符
     * @param name 实体名称
     * @return 新创建的实体
     */
    Entity createEntityWithUUID(uint64_t uuid, const std::string& name = "Entity");

    /**
     * @brief 销毁实体
     * @param entity 要销毁的实体
     */
    void destroyEntity(Entity entity);

    /**
     * @brief 复制实体
     * @param entity 要复制的实体
     * @return 新复制的实体
     */
    Entity duplicateEntity(Entity entity);

    /**
     * @brief 根据名称查找实体
     * @param name 实体名称
     * @return 找到的实体，如果没找到返回无效实体
     */
    Entity findEntityByName(const std::string& name);

    /**
     * @brief 根据 UUID 查找实体
     * @param uuid 唯一标识符
     * @return 找到的实体，如果没找到返回无效实体
     */
    Entity findEntityByUUID(uint64_t uuid);

    /**
     * @brief 获取所有实体
     * @return 实体列表
     */
    std::vector<Entity> getAllEntities();

    /**
     * @brief 获取根实体（没有父节点的实体）
     * @return 根实体列表
     */
    std::vector<Entity> getRootEntities();

    // ============================================================
    // 场景生命周期
    // ============================================================
    
    /**
     * @brief 场景开始时调用
     */
    void onStart();

    /**
     * @brief 每帧更新
     * @param deltaTime 帧间隔时间
     */
    void onUpdate(float deltaTime);

    /**
     * @brief 场景结束时调用
     */
    void onStop();

    /**
     * @brief 窗口大小改变时调用
     * @param width 新宽度
     * @param height 新高度
     */
    void onViewportResize(uint32_t width, uint32_t height);

    // ============================================================
    // 访问器
    // ============================================================
    
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    entt::registry& getRegistry() { return m_registry; }
    const entt::registry& getRegistry() const { return m_registry; }

    bool isRunning() const { return m_isRunning; }
    bool isPaused() const { return m_isPaused; }

    void setPaused(bool paused) { m_isPaused = paused; }

    uint32_t getViewportWidth() const { return m_viewportWidth; }
    uint32_t getViewportHeight() const { return m_viewportHeight; }

    // ============================================================
    // 主相机
    // ============================================================
    
    /**
     * @brief 获取主相机实体
     * @return 主相机实体，如果没有返回无效实体
     */
    Entity getPrimaryCameraEntity();

private:
    friend class Entity;
    friend class SceneSerializer;
    friend class SceneHierarchyPanel;

    /**
     * @brief 生成新的 UUID
     */
    uint64_t generateUUID();

private:
    std::string m_name;
    entt::registry m_registry;
    
    bool m_isRunning = false;
    bool m_isPaused = false;
    
    uint32_t m_viewportWidth = 1280;
    uint32_t m_viewportHeight = 720;

    uint64_t m_nextUUID = 1;  // 简单的 UUID 计数器
};

} // namespace VulkanEngine
