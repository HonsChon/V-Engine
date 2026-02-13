
#include "DebugPanel.h"
#include "imgui.h"

DebugPanel::DebugPanel() {
    // 初始化 FPS 历史记录
    for (int i = 0; i < FPS_HISTORY_SIZE; ++i) {
        fpsHistory[i] = 0.0f;
    }
}

void DebugPanel::render() {
    // 更新 FPS 历史
    fpsHistory[fpsHistoryIndex] = fps;
    fpsHistoryIndex = (fpsHistoryIndex + 1) % FPS_HISTORY_SIZE;

    ImGui::Begin("Debug Panel", nullptr, ImGuiWindowFlags_NoCollapse);

    // === 性能统计 ===
    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        // FPS 和帧时间
        ImGui::Text("FPS: %.1f", fps);
        ImGui::SameLine(150);
        ImGui::Text("Frame Time: %.2f ms", frameTime);

        // FPS 图表
        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%.1f FPS", fps);
        ImGui::PlotLines("##FPS", fpsHistory, FPS_HISTORY_SIZE, fpsHistoryIndex, 
                         overlay, 0.0f, 120.0f, ImVec2(0, 50));

        ImGui::Separator();

        // 渲染统计
        ImGui::Text("Draw Calls: %u", drawCalls);
        ImGui::Text("Triangles: %u", triangles);
        ImGui::Text("Vertices: %u", vertices);
        
        // GPU 内存使用
        if (gpuMemory > 0) {
            float memoryMB = static_cast<float>(gpuMemory) / (1024.0f * 1024.0f);
            ImGui::Text("GPU Memory: %.2f MB", memoryMB);
        }
    }

    ImGui::Spacing();

    // === 相机信息 ===
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Position:");
        ImGui::SameLine(80);
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), 
                          "X: %.2f  Y: %.2f  Z: %.2f", 
                          cameraPosition.x, cameraPosition.y, cameraPosition.z);

        ImGui::Text("Rotation:");
        ImGui::SameLine(80);
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), 
                          "Pitch: %.1f  Yaw: %.1f", 
                          cameraRotation.x, cameraRotation.y);

        ImGui::Text("FOV:");
        ImGui::SameLine(80);
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%.1f°", cameraFOV);
    }

    ImGui::Spacing();

    // === 场景信息 ===
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Scene Name:");
        ImGui::SameLine(100);
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "%s", sceneName.c_str());

        ImGui::Text("Objects:");
        ImGui::SameLine(100);
        ImGui::Text("%d", objectCount);

        ImGui::Text("Render Mode:");
        ImGui::SameLine(100);
        
        // 根据渲染模式显示不同颜色
        if (renderMode == "Deferred") {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%s", renderMode.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "%s", renderMode.c_str());
        }
    }

    ImGui::Spacing();

    // === 控制说明 ===
    if (ImGui::CollapsingHeader("Controls")) {
        ImGui::BulletText("W/A/S/D - Move camera");
        ImGui::BulletText("Space/Shift - Up/Down");
        ImGui::BulletText("Right Mouse + Drag - Look around");
        ImGui::BulletText("Scroll - Adjust FOV");
        ImGui::BulletText("1-5 - Switch geometry/scene");
        ImGui::BulletText("ESC - Exit");
    }

    ImGui::End();
}
