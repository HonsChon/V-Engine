
#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

/**
 * DebugPanel - 调试信息面板
 * 
 * 显示实时渲染统计信息、相机状态等调试数据。
 */
class DebugPanel {
public:
    DebugPanel();
    ~DebugPanel() = default;

    /**
     * 渲染面板
     */
    void render();

    // 设置渲染统计
    void setFPS(float fps) { this->fps = fps; }
    void setFrameTime(float ms) { frameTime = ms; }
    void setDrawCalls(uint32_t count) { drawCalls = count; }
    void setTriangles(uint32_t count) { triangles = count; }
    void setVertices(uint32_t count) { vertices = count; }
    void setGPUMemory(size_t bytes) { gpuMemory = bytes; }

    // 设置相机信息
    void setCameraPosition(const glm::vec3& pos) { cameraPosition = pos; }
    void setCameraRotation(const glm::vec3& rot) { cameraRotation = rot; }
    void setCameraFOV(float fov) { cameraFOV = fov; }

    // 设置场景信息
    void setSceneName(const std::string& name) { sceneName = name; }
    void setObjectCount(int count) { objectCount = count; }
    void setRenderMode(const std::string& mode) { renderMode = mode; }

private:
    // 渲染统计
    float fps = 0.0f;
    float frameTime = 0.0f;
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t vertices = 0;
    size_t gpuMemory = 0;

    // FPS 历史记录（用于图表）
    static constexpr int FPS_HISTORY_SIZE = 120;
    float fpsHistory[FPS_HISTORY_SIZE] = {};
    int fpsHistoryIndex = 0;

    // 相机信息
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    glm::vec3 cameraRotation = glm::vec3(0.0f);
    float cameraFOV = 45.0f;

    // 场景信息
    std::string sceneName = "Untitled";
    int objectCount = 0;
    std::string renderMode = "Forward";
};
