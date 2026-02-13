
#pragma once

#include <string>
#include <vector>
#include <functional>

/**
 * SceneHierarchyPanel - 场景层级面板
 * 
 * 显示场景中所有对象的层级结构，支持选择对象。
 */
class SceneHierarchyPanel {
public:
    // 场景对象信息
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
    void renderObjectNode(const SceneObject& obj, int depth = 0);

    std::vector<SceneObject> sceneObjects;
    int selectedObjectId = -1;
    std::function<void(int)> onSelectionChanged;

    // 搜索过滤
    char searchFilter[128] = "";
};
