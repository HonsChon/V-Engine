
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

// 前向声明
class ImGuiLayer;
class DebugPanel;
class SceneHierarchyPanel;
class InspectorPanel;
class AssetBrowserPanel;
class Camera;
struct RenderSettings;

namespace VEngine {
    class Scene;
}

/**
 * UIManager - UI 管理器
 * 
 * 协调所有UI 面板的更新和渲染。
 * 提供统一的接口来管理编辑器UI 状态。
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
     * 渲染所有UI 面板
     * 在ImGuiLayer::beginFrame() 和endFrame() 之间调用
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
     * 设置各面板的可见态
     */
    void setDebugPanelVisible(bool visible);
    void setSceneHierarchyVisible(bool visible);
    void setInspectorVisible(bool visible);
    void setAssetBrowserVisible(bool visible);

    /**
     * 获取各面板的可见态
     */
    bool isDebugPanelVisible() const;
    bool isSceneHierarchyVisible() const;
    bool isInspectorVisible() const;
    bool isAssetBrowserVisible() const;

    /**
     * 切换面板可见态
     */
    void toggleDebugPanel();
    void toggleSceneHierarchy();
    void toggleInspector();
    void toggleAssetBrowser();

    /**
     * 设置渲染选项指针（SSAO 等开关控制）
     */
    void setRenderSettings(RenderSettings* settings);

    // 获取面板引用（用于外部访问面板数据）
    DebugPanel* getDebugPanel() { return debugPanel.get(); }
    SceneHierarchyPanel* getSceneHierarchyPanel() { return sceneHierarchyPanel.get(); }
    InspectorPanel* getInspectorPanel() { return inspectorPanel.get(); }
    AssetBrowserPanel* getAssetBrowserPanel() { return assetBrowserPanel.get(); }

    // ============================================================
    // ECS 集成
    // ============================================================

    /**
     * 设置 ECS 场景（启用ECS 模式：
     */
    void setScene(VEngine::Scene* scene);

    /**
     * 获取当前场景
     */
    VEngine::Scene* getScene() const { return m_scene; }

    /**
     * 设置选中的实体
     */
    void setSelectedEntity(entt::entity entity);

    /**
     * 获取选中的实体
     */
    entt::entity getSelectedEntity() const;

    // ============================================================
    // 场景文件动作（File 菜单 / 快捷键 → 回调由 Engine 注入）
    // ============================================================

    using ActionCallback = std::function<void()>;

    void setOnNewScene(ActionCallback cb)     { onNewScene = std::move(cb); }
    void setOnOpenScene(ActionCallback cb)    { onOpenScene = std::move(cb); }
    void setOnSaveScene(ActionCallback cb)    { onSaveScene = std::move(cb); }
    void setOnSaveSceneAs(ActionCallback cb)  { onSaveSceneAs = std::move(cb); }
    void setOnExit(ActionCallback cb)         { onExit = std::move(cb); }

    /**
     * @brief 设置菜单栏显示的场景标题（文件名 + 修改标记）
     */
    void setSceneTitle(const std::string& title) { m_sceneTitle = title; }

private:
    void renderMainMenuBar();
    void handleGlobalShortcuts();   // Ctrl+N/O/S（ImGui 键盘路由）

    std::unique_ptr<DebugPanel> debugPanel;
    std::unique_ptr<SceneHierarchyPanel> sceneHierarchyPanel;
    std::unique_ptr<InspectorPanel> inspectorPanel;
    std::unique_ptr<AssetBrowserPanel> assetBrowserPanel;

    // 场景动作回调（Engine 注入）
    ActionCallback onNewScene;
    ActionCallback onOpenScene;
    ActionCallback onSaveScene;
    ActionCallback onSaveSceneAs;
    ActionCallback onExit;
    std::string m_sceneTitle = "Untitled";

    // 面板可见态
    bool showDebugPanel = true;
    bool showSceneHierarchy = true;
    bool showInspector = true;
    bool showAssetBrowser = false;  // 默认隐藏

    // 显示 ImGui Demo（调试用：
    bool showImGuiDemo = false;

    // ECS 数据
    VEngine::Scene* m_scene = nullptr;
};
