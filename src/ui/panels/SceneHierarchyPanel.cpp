#include "SceneHierarchyPanel.h"
#include "imgui.h"
#include "../../scene/Scene.h"
#include "../../scene/Entity.h"
#include "../../scene/Components.h"
#include "../../scene/SelectionManager.h"
#include <algorithm>
#include <cstring>

using namespace VulkanEngine;

SceneHierarchyPanel::SceneHierarchyPanel() {
    // 添加一些示例对象用于测试
    addObject(0, "Main Camera", "Camera");
    addObject(1, "Directional Light", "Light");
    addObject(2, "Sphere", "Mesh");
    addObject(3, "Ground Plane", "Mesh");
}

void SceneHierarchyPanel::render() {
    ImGui::Begin("Scene Hierarchy", nullptr, ImGuiWindowFlags_NoCollapse);

    // 搜索框
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Search", "Search objects...", searchFilter, sizeof(searchFilter));

    ImGui::Separator();

    // 根据模式渲染
    if (ImGui::BeginChild("ObjectList", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        if (m_useECSMode && m_scene) {
            renderECSHierarchy();
        } else {
            // 旧系统的对象列表
            for (const auto& obj : sceneObjects) {
                // 如果有搜索过滤，检查名称是否匹配
                if (strlen(searchFilter) > 0) {
                    std::string lowerName = obj.name;
                    std::string lowerFilter = searchFilter;
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
                    
                    if (lowerName.find(lowerFilter) == std::string::npos) {
                        continue;
                    }
                }

                renderObjectNode(obj);
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void SceneHierarchyPanel::setScene(VulkanEngine::Scene* scene) {
    m_scene = scene;
    m_useECSMode = (scene != nullptr);
    
    // 同步选择管理器
    if (scene) {
        SelectionManager::getInstance().setScene(scene);
    }
}

void SceneHierarchyPanel::setSelectedEntity(entt::entity entity) {
    m_selectedEntity = entity;
    SelectionManager::getInstance().select(entity);
}

void SceneHierarchyPanel::renderECSHierarchy() {
    if (!m_scene) return;

    auto& registry = m_scene->getRegistry();
    
    // 获取所有有 TagComponent 的实体（根节点：没有父级的实体）
    auto view = registry.view<TagComponent>();
    
    for (auto entity : view) {
        // 检查是否有父级，如果有则跳过（将在父级的子树中渲染）
        if (registry.all_of<RelationshipComponent>(entity)) {
            auto& relationship = registry.get<RelationshipComponent>(entity);
            if (relationship.parent != entt::null) {
                continue;  // 跳过有父级的实体
            }
        }
        
        // 搜索过滤
        if (strlen(searchFilter) > 0) {
            auto& tag = registry.get<TagComponent>(entity);
            std::string lowerName = tag.tag;
            std::string lowerFilter = searchFilter;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
            
            if (lowerName.find(lowerFilter) == std::string::npos) {
                continue;
            }
        }
        
        renderEntityNode(entity);
    }

    // 右键菜单 - 在空白区域
    if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Empty Entity")) {
            auto newEntity = m_scene->createEntity("Empty Entity");
            setSelectedEntity(newEntity.getHandle());
        }
        if (ImGui::MenuItem("Create Cube")) {
            auto newEntity = m_scene->createEntity("Cube");
            newEntity.addComponent<MeshRendererComponent>();
            setSelectedEntity(newEntity.getHandle());
        }
        if (ImGui::MenuItem("Create Light")) {
            auto newEntity = m_scene->createEntity("Light");
            newEntity.addComponent<LightComponent>();
            setSelectedEntity(newEntity.getHandle());
        }
        if (ImGui::MenuItem("Create Camera")) {
            auto newEntity = m_scene->createEntity("Camera");
            newEntity.addComponent<CameraComponent>();
            setSelectedEntity(newEntity.getHandle());
        }
        ImGui::EndPopup();
    }
}

void SceneHierarchyPanel::renderEntityNode(entt::entity entity) {
    if (!m_scene) return;

    auto& registry = m_scene->getRegistry();
    
    // 获取实体名称
    std::string name = "Entity";
    if (registry.all_of<TagComponent>(entity)) {
        name = registry.get<TagComponent>(entity).tag;
    }

    // 检查是否有子节点
    bool hasChildren = false;
    entt::entity firstChild = entt::null;
    if (registry.all_of<RelationshipComponent>(entity)) {
        auto& relationship = registry.get<RelationshipComponent>(entity);
        hasChildren = (relationship.firstChild != entt::null);
        firstChild = relationship.firstChild;
    }

    // 设置树节点标志
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | 
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;

    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    if (m_selectedEntity == entity) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // 根据组件类型选择图标
    const char* icon = "[E] ";
    if (registry.all_of<CameraComponent>(entity)) {
        icon = "[C] ";
    } else if (registry.all_of<LightComponent>(entity)) {
        icon = "[L] ";
    } else if (registry.all_of<MeshRendererComponent>(entity)) {
        icon = "[M] ";
    }

    // 构建显示名称
    std::string displayName = icon + name;

    // 使用实体 ID 作为唯一标识
    bool nodeOpen = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uint64_t>(static_cast<uint32_t>(entity))),
        flags, 
        "%s", 
        displayName.c_str()
    );

    // 点击选中
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        if (m_selectedEntity != entity) {
            m_selectedEntity = entity;
            SelectionManager::getInstance().select(entity);
            
            if (m_onEntitySelected) {
                m_onEntitySelected(entity);
            }
        }
    }

    // 右键菜单
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Create Child")) {
            auto child = m_scene->createEntity("Child");
            Entity childEntity(child.getHandle(), m_scene);
            Entity parentEntity(entity, m_scene);
            childEntity.setParent(parentEntity);
            setSelectedEntity(child.getHandle());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
            m_scene->destroyEntity(Entity(entity, m_scene));
            if (m_selectedEntity == entity) {
                m_selectedEntity = entt::null;
                SelectionManager::getInstance().clearSelection();
            }
        }
        if (ImGui::MenuItem("Duplicate")) {
            // TODO: 实现复制功能
        }
        ImGui::EndPopup();
    }

    // 拖放支持
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("ENTITY", &entity, sizeof(entt::entity));
        ImGui::Text("Moving: %s", name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
            entt::entity droppedEntity = *(const entt::entity*)payload->Data;
            if (droppedEntity != entity) {
                Entity dropped(droppedEntity, m_scene);
                Entity target(entity, m_scene);
                dropped.setParent(target);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // 递归渲染子节点
    if (nodeOpen && hasChildren) {
        entt::entity child = firstChild;
        while (child != entt::null) {
            renderEntityNode(child);
            
            // 获取下一个兄弟节点
            if (registry.all_of<RelationshipComponent>(child)) {
                child = registry.get<RelationshipComponent>(child).nextSibling;
            } else {
                break;
            }
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::renderObjectNode(const SceneObject& obj, int depth) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | 
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;

    // 如果没有子对象，显示为叶子节点
    if (obj.childrenIds.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    // 如果是选中的对象
    if (selectedObjectId == obj.id) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // 根据类型选择图标
    const char* icon = "";
    if (obj.type == "Camera") {
        icon = "[C] ";
    } else if (obj.type == "Light") {
        icon = "[L] ";
    } else if (obj.type == "Mesh") {
        icon = "[M] ";
    } else {
        icon = "[?] ";
    }

    // 缩进
    if (depth > 0) {
        ImGui::Indent(depth * 10.0f);
    }

    // 设置可见性颜色
    if (!obj.visible) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    }

    // 构建显示名称
    std::string displayName = icon + obj.name;

    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj.id, flags, "%s", displayName.c_str());

    // 恢复颜色
    if (!obj.visible) {
        ImGui::PopStyleColor();
    }

    // 点击选中
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        if (selectedObjectId != obj.id) {
            selectedObjectId = obj.id;
            if (onSelectionChanged) {
                onSelectionChanged(selectedObjectId);
            }
        }
    }

    // 右键菜单
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete")) {
            // TODO: 删除对象
        }
        if (ImGui::MenuItem("Duplicate")) {
            // TODO: 复制对象
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename")) {
            // TODO: 重命名
        }
        ImGui::EndPopup();
    }

    // 递归渲染子对象
    if (nodeOpen && !obj.childrenIds.empty()) {
        // TODO: 根据 childrenIds 找到子对象并渲染
        ImGui::TreePop();
    }

    // 取消缩进
    if (depth > 0) {
        ImGui::Unindent(depth * 10.0f);
    }
}

void SceneHierarchyPanel::setSceneObjects(const std::vector<SceneObject>& objects) {
    sceneObjects = objects;
}

void SceneHierarchyPanel::addObject(int id, const std::string& name, const std::string& type) {
    SceneObject obj;
    obj.id = id;
    obj.name = name;
    obj.type = type;
    obj.visible = true;
    sceneObjects.push_back(obj);
}

void SceneHierarchyPanel::clearObjects() {
    sceneObjects.clear();
    selectedObjectId = -1;
}