#pragma once

#include <glm/glm.hpp>
#include <string>
#include <functional>
#include <entt/entt.hpp>

namespace VulkanEngine {
    class Scene;
}

/**
 * InspectorPanel - 属性检查器面板
 * 
 * 显示和编辑选中对象的属性，包括变换、材质等。
 * 现已与 ECS (EnTT) 系统集成。
 */
class InspectorPanel {
public:
    // 变换数据
    struct Transform {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 rotation = glm::vec3(0.0f);  // 欧拉角（度）
        glm::vec3 scale = glm::vec3(1.0f);
    };

    // 材质数据
    struct MaterialData {
        glm::vec3 albedo = glm::vec3(1.0f);
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
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

    InspectorPanel();
    ~InspectorPanel() = default;

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
     * 设置选中的 ECS 实体
     */
    void setSelectedEntity(entt::entity entity);

    /**
     * 获取当前选中的 ECS 实体
     */
    entt::entity getSelectedEntity() const { return m_selectedEntity; }

    // ============================================================
    // 旧系统兼容（可在迁移后移除）
    // ============================================================

    /**
     * 设置当前选中对象的信息
     */
    void setSelectedObject(int id, const std::string& name, const std::string& type);

    /**
     * 清除选中
     */
    void clearSelection();

    /**
     * 设置变换数据
     */
    void setTransform(const Transform& transform);

    /**
     * 获取变换数据（可能被用户修改）
     */
    const Transform& getTransform() const { return currentTransform; }

    /**
     * 设置材质数据
     */
    void setMaterial(const MaterialData& material);

    /**
     * 获取材质数据
     */
    const MaterialData& getMaterial() const { return currentMaterial; }

    /**
     * 设置光照数据
     */
    void setLight(const LightData& light);

    /**
     * 设置变换改变时的回调
     */
    void setOnTransformChanged(std::function<void(const Transform&)> callback) {
        onTransformChanged = callback;
    }

    /**
     * 设置材质改变时的回调
     */
    void setOnMaterialChanged(std::function<void(const MaterialData&)> callback) {
        onMaterialChanged = callback;
    }

private:
    // ECS 模式渲染
    void renderECSInspector();
    void renderTagComponent();
    void renderTransformComponent();
    void renderMeshRendererComponent();
    void renderLightComponent();
    void renderCameraComponent();
    void renderAddComponentButton();

    // 旧系统渲染
    void renderTransformSection();
    void renderMaterialSection();
    void renderLightSection();

    // ECS 数据
    VulkanEngine::Scene* m_scene = nullptr;
    entt::entity m_selectedEntity = entt::null;
    bool m_useECSMode = false;

    // 选中对象信息（旧系统）
    int selectedId = -1;
    std::string selectedName;
    std::string selectedType;

    // 对象属性（旧系统）
    Transform currentTransform;
    MaterialData currentMaterial;
    LightData currentLight;

    // 回调函数
    std::function<void(const Transform&)> onTransformChanged;
    std::function<void(const MaterialData&)> onMaterialChanged;
};