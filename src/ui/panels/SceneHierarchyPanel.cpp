
#include "SceneHierarchyPanel.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>

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

    // 对象列表
    if (ImGui::BeginChild("ObjectList", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
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
    ImGui::EndChild();

    ImGui::End();
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
