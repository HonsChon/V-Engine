#include "SelectionManager.h"
#include "Scene.h"
#include "Entity.h"
#include "Components.h"
#include <algorithm>

namespace VulkanEngine {

SelectionManager& SelectionManager::getInstance() {
    static SelectionManager instance;
    return instance;
}

void SelectionManager::select(entt::entity entity) {
    m_selectedEntities.clear();
    if (entity != entt::null) {
        m_selectedEntities.push_back(entity);
    }
    notifySelectionChanged();
}

void SelectionManager::addToSelection(entt::entity entity) {
    if (entity == entt::null) return;
    
    if (!isSelected(entity)) {
        m_selectedEntities.push_back(entity);
        notifySelectionChanged();
    }
}

void SelectionManager::removeFromSelection(entt::entity entity) {
    auto it = std::find(m_selectedEntities.begin(), m_selectedEntities.end(), entity);
    if (it != m_selectedEntities.end()) {
        m_selectedEntities.erase(it);
        notifySelectionChanged();
    }
}

void SelectionManager::toggleSelection(entt::entity entity) {
    if (isSelected(entity)) {
        removeFromSelection(entity);
    } else {
        addToSelection(entity);
    }
}

void SelectionManager::clearSelection() {
    if (!m_selectedEntities.empty()) {
        m_selectedEntities.clear();
        notifySelectionChanged();
    }
}

void SelectionManager::selectAll(Scene* scene) {
    if (!scene) return;
    
    m_selectedEntities.clear();
    auto& registry = scene->getRegistry();
    
    // 选择所有有 TagComponent 的实体
    auto view = registry.view<TagComponent>();
    for (auto entity : view) {
        m_selectedEntities.push_back(entity);
    }
    
    notifySelectionChanged();
}

entt::entity SelectionManager::getSelectedEntity() const {
    if (m_selectedEntities.empty()) {
        return entt::null;
    }
    return m_selectedEntities[0];
}

bool SelectionManager::isSelected(entt::entity entity) const {
    return std::find(m_selectedEntities.begin(), m_selectedEntities.end(), entity) 
           != m_selectedEntities.end();
}

void SelectionManager::onSelectionChanged(SelectionChangedCallback callback) {
    m_callbacks.push_back(callback);
}

void SelectionManager::notifySelectionChanged() {
    entt::entity selectedEntity = getSelectedEntity();
    for (auto& callback : m_callbacks) {
        callback(selectedEntity);
    }
}

} // namespace VulkanEngine
