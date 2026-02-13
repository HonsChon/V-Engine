
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <glm/glm.hpp>

// 前向声明
class ImGuiLayer;
class DebugPanel;
class SceneHierarchyPanel;
class InspectorPanel;
class AssetBrowserPanel;
class Camera;

/**
 * UIManager - UI 管理器
 * 
 * 协调所有 UI 面板的更新和渲染。
 * 提供统一的接口来管理编辑器 UI 状态。
 */
class UIManager {
public:
    // 渲染统计信息
    struct RenderStats {
        float fps = 0.0f;
        float frameTime = 0.0f;      // 毫秒
        uint32_t drawCalls = 0;
        uint32_t triangles = 0;
        uint32_t vertices = 0;
        size_t gpuMemoryUsed = 0;    // 字节
    };

    // 场景信息
    struct SceneInfo {
        std::string currentSceneName = "Untitled";
        int objectCount = 0;
        int lightCount = 0;
        bool isWaterScene = false;
        bool isDeferredMode = false;
    };

    UIManager();
    ~UIManager();

    // 禁止拷贝
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    /**
     * 渲染所有 UI 面板
     * 在 ImGuiLayer::beginFrame() 和 endFrame() 之间调用
     */
    void render();

    /**
     * 更新渲染统计信息
     */
    void updateRenderStats(const RenderStats& stats);

    /**
     * 更新相机信息
     */
    void updateCameraInfo(const glm::vec3& position, const glm::vec3& rotation, float fov);

    /**
     * 更新场景信息
     */
    void updateSceneInfo(const SceneInfo& info);

    /**
     * 设置各面板的可见性
     */
    void setDebugPanelVisible(bool visible);
    void setSceneHierarchyVisible(bool visible);
    void setInspectorVisible(bool visible);
    void setAssetBrowserVisible(bool visible);

    /**
     * 获取各面板的可见性
     */
    bool isDebugPanelVisible() const;
    bool isSceneHierarchyVisible() const;
    bool isInspectorVisible() const;
    bool isAssetBrowserVisible() const;

    /**
     * 切换面板可见性
     */
    void toggleDebugPanel();
    void toggleSceneHierarchy();
    void toggleInspector();
    void toggleAssetBrowser();

    // 获取面板引用（用于外部访问面板数据）
    DebugPanel* getDebugPanel() { return debugPanel.get(); }
    SceneHierarchyPanel* getSceneHierarchyPanel() { return sceneHierarchyPanel.get(); }
    InspectorPanel* getInspectorPanel() { return inspectorPanel.get(); }
    AssetBrowserPanel* getAssetBrowserPanel() { return assetBrowserPanel.get(); }

private:
    void renderMainMenuBar();

    std::unique_ptr<DebugPanel> debugPanel;
    std::unique_ptr<SceneHierarchyPanel> sceneHierarchyPanel;
    std::unique_ptr<InspectorPanel> inspectorPanel;
    std::unique_ptr<AssetBrowserPanel> assetBrowserPanel;

    // 面板可见性
    bool showDebugPanel = true;
    bool showSceneHierarchy = true;
    bool showInspector = true;
    bool showAssetBrowser = false;  // 默认隐藏

    // 显示 ImGui Demo（调试用）
    bool showImGuiDemo = false;
};
