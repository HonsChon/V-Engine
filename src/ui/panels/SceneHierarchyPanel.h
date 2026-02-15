#pragma once

#include <string>
#include <vector>
#include <functional>
#include <entt/entt.hpp>

namespace VulkanEngine {
    class Scene;
    class Entity;
}

/**
 * SceneHierarchyPanel - 场景层级面板
 * 
 * 显示场景中所有对象的层级结构，支持选择对象。
 * 现已与 ECS (EnTT) 系统集成。
 */
class SceneHierarchyPanel {
public:
    // 场景对象信息（保留用于兼容旧系统）
    struct SceneObject {
        int id;
        std::string name;
        std::string type;  // "Mesh", "Light", "Camera" 等
        bool visible = true;
        std::vector<int> childrenIds;
    };

    SceneHierarchyPanel();
    ~SceneHierarchyPanel() = default;

    /**
     * 渲染面板
     */
    void render();

    // ============================================================
    // ECS 集成
    // ============================================================
    
    /**
     * 设置 ECS 场景
     */
    void setScene(VulkanEngine::Scene* scene);

    /**
     * 获取当前选中的 ECS 实体
     */
    entt::entity getSelectedEntity() const { return m_selectedEntity; }

    /**
     * 设置选中的 ECS 实体
     */
    void setSelectedEntity(entt::entity entity);

    /**
     * 设置 ECS 实体选中回调
     */
    void setOnEntitySelected(std::function<void(entt::entity)> callback) {
        m_onEntitySelected = callback;
    }

    // ============================================================
    // 旧系统兼容（可在迁移后移除）
    // ============================================================

    /**
     * 设置场景对象列表
     */
    void setSceneObjects(const std::vector<SceneObject>& objects);

    /**
     * 获取当前选中的对象 ID（-1 表示未选中）
     */
    int getSelectedObjectId() const { return selectedObjectId; }

    /**
     * 设置选中对象改变时的回调
     */
    void setOnSelectionChanged(std::function<void(int)> callback) {
        onSelectionChanged = callback;
    }

    /**
     * 添加简单对象（用于快速测试）
     */
    void addObject(int id, const std::string& name, const std::string& type);

    /**
     * 清空对象列表
     */
    void clearObjects();

private:
    // ECS 渲染方法
    void renderECSHierarchy();
    void renderEntityNode(entt::entity entity);

    // 旧系统渲染方法
    void renderObjectNode(const SceneObject& obj, int depth = 0);

    // ECS 数据
    VulkanEngine::Scene* m_scene = nullptr;
    entt::entity m_selectedEntity = entt::null;
    std::function<void(entt::entity)> m_onEntitySelected;

    // 旧系统数据
    std::vector<SceneObject> sceneObjects;
    int selectedObjectId = -1;
    std::function<void(int)> onSelectionChanged;

    // 搜索过滤
    char searchFilter[128] = "";

    // 是否使用 ECS 模式
    bool m_useECSMode = false;
};