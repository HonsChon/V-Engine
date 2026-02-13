
#include "UIManager.h"
#include "panels/DebugPanel.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/AssetBrowserPanel.h"

#include "imgui.h"

UIManager::UIManager() {
    debugPanel = std::make_unique<DebugPanel>();
    sceneHierarchyPanel = std::make_unique<SceneHierarchyPanel>();
    inspectorPanel = std::make_unique<InspectorPanel>();
    assetBrowserPanel = std::make_unique<AssetBrowserPanel>();
}

UIManager::~UIManager() = default;

void UIManager::render() {
    // 渲染主菜单栏
    renderMainMenuBar();

    // 渲染各个面板
    if (showDebugPanel && debugPanel) {
        debugPanel->render();
    }

    if (showSceneHierarchy && sceneHierarchyPanel) {
        sceneHierarchyPanel->render();
    }

    if (showInspector && inspectorPanel) {
        inspectorPanel->render();
    }

    if (showAssetBrowser && assetBrowserPanel) {
        assetBrowserPanel->render();
    }

    // ImGui Demo 窗口（调试用）
    if (showImGuiDemo) {
        ImGui::ShowDemoWindow(&showImGuiDemo);
    }
}

void UIManager::renderMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                // TODO: 新建场景
            }
            if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
                // TODO: 打开场景
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                // TODO: 保存场景
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                // TODO: 退出程序
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences")) {
                // TODO: 打开偏好设置
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Debug Panel", "F1", &showDebugPanel);
            ImGui::MenuItem("Scene Hierarchy", "F2", &showSceneHierarchy);
            ImGui::MenuItem("Inspector", "F3", &showInspector);
            ImGui::MenuItem("Asset Browser", "F4", &showAssetBrowser);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &showImGuiDemo);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About V Engine")) {
                // TODO: 显示关于对话框
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void UIManager::updateRenderStats(const RenderStats& stats) {
    if (debugPanel) {
        debugPanel->setFPS(stats.fps);
        debugPanel->setFrameTime(stats.frameTime);
        debugPanel->setDrawCalls(stats.drawCalls);
        debugPanel->setTriangles(stats.triangles);
        debugPanel->setVertices(stats.vertices);
        debugPanel->setGPUMemory(stats.gpuMemoryUsed);
    }
}

void UIManager::updateCameraInfo(const glm::vec3& position, const glm::vec3& rotation, float fov) {
    if (debugPanel) {
        debugPanel->setCameraPosition(position);
        debugPanel->setCameraRotation(rotation);
        debugPanel->setCameraFOV(fov);
    }
}

void UIManager::updateSceneInfo(const SceneInfo& info) {
    if (debugPanel) {
        debugPanel->setSceneName(info.currentSceneName);
        debugPanel->setObjectCount(info.objectCount);
        debugPanel->setRenderMode(info.isDeferredMode ? "Deferred" : "Forward");
    }
}

void UIManager::setDebugPanelVisible(bool visible) { showDebugPanel = visible; }
void UIManager::setSceneHierarchyVisible(bool visible) { showSceneHierarchy = visible; }
void UIManager::setInspectorVisible(bool visible) { showInspector = visible; }
void UIManager::setAssetBrowserVisible(bool visible) { showAssetBrowser = visible; }

bool UIManager::isDebugPanelVisible() const { return showDebugPanel; }
bool UIManager::isSceneHierarchyVisible() const { return showSceneHierarchy; }
bool UIManager::isInspectorVisible() const { return showInspector; }
bool UIManager::isAssetBrowserVisible() const { return showAssetBrowser; }

void UIManager::toggleDebugPanel() { showDebugPanel = !showDebugPanel; }
void UIManager::toggleSceneHierarchy() { showSceneHierarchy = !showSceneHierarchy; }
void UIManager::toggleInspector() { showInspector = !showInspector; }
void UIManager::toggleAssetBrowser() { showAssetBrowser = !showAssetBrowser; }
