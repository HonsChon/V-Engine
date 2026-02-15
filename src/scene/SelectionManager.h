#pragma once

#include <entt/entt.hpp>
#include <functional>
#include <vector>

namespace VulkanEngine {

class Scene;
class Entity;

/**
 * @brief 选择管理器 - 管理场景中实体的选中状态
 */
class SelectionManager {
public:
    using SelectionChangedCallback = std::function<void(entt::entity)>;

    /**
     * @brief 获取单例实例
     */
    static SelectionManager& getInstance();

    // 禁止复制和移动
    SelectionManager(const SelectionManager&) = delete;
    SelectionManager& operator=(const SelectionManager&) = delete;

    // ============================================================
    // 选择操作
    // ============================================================

    /**
     * @brief 选中实体
     * @param entity 要选中的实体
     */
    void select(entt::entity entity);

    /**
     * @brief 添加到选中列表（多选）
     * @param entity 要添加的实体
     */
    void addToSelection(entt::entity entity);

    /**
     * @brief 从选中列表移除
     * @param entity 要移除的实体
     */
    void removeFromSelection(entt::entity entity);

    /**
     * @brief 切换选中状态
     * @param entity 实体
     */
    void toggleSelection(entt::entity entity);

    /**
     * @brief 清除所有选中
     */
    void clearSelection();

    /**
     * @brief 全选
     * @param scene 场景
     */
    void selectAll(Scene* scene);

    // ============================================================
    // 查询
    // ============================================================

    /**
     * @brief 获取当前选中的实体（单选模式下的第一个）
     */
    entt::entity getSelectedEntity() const;

    /**
     * @brief 获取所有选中的实体
     */
    const std::vector<entt::entity>& getSelectedEntities() const { return m_selectedEntities; }

    /**
     * @brief 检查实体是否被选中
     */
    bool isSelected(entt::entity entity) const;

    /**
     * @brief 是否有选中的实体
     */
    bool hasSelection() const { return !m_selectedEntities.empty(); }

    /**
     * @brief 获取选中数量
     */
    size_t getSelectionCount() const { return m_selectedEntities.size(); }

    // ============================================================
    // 回调
    // ============================================================

    /**
     * @brief 注册选择变更回调
     */
    void onSelectionChanged(SelectionChangedCallback callback);

    // ============================================================
    // 设置
    // ============================================================

    /**
     * @brief 设置关联的场景
     */
    void setScene(Scene* scene) { m_scene = scene; }

    /**
     * @brief 获取关联的场景
     */
    Scene* getScene() const { return m_scene; }

private:
    SelectionManager() = default;
    ~SelectionManager() = default;

    void notifySelectionChanged();

private:
    std::vector<entt::entity> m_selectedEntities;
    std::vector<SelectionChangedCallback> m_callbacks;
    Scene* m_scene = nullptr;
};

} // namespace VulkanEngine
