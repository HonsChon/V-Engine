
#include "DebugPanel.h"
#include "imgui.h"
#include "RenderSettings.h"

DebugPanel::DebugPanel() {
    // 初始区FPS 历史记录
    for (int i = 0; i < FPS_HISTORY_SIZE; ++i) {
        fpsHistory[i] = 0.0f;
    }
}

void DebugPanel::setNaniteStats(uint32_t total, uint32_t visible, uint32_t drawn,
                                uint32_t triangles, uint32_t vertices,
                                const uint32_t* lodCounts, int lodCount) {
    naniteTotal = total;
    naniteVisible = visible;
    naniteDrawn = drawn;
    naniteTriangles = triangles;
    naniteVertices = vertices;
    for (int i = 0; i < NANITE_MAX_LOD; ++i) {
        naniteLodCounts[i] = (lodCounts && i < lodCount) ? lodCounts[i] : 0;
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

    // === SSAO 设置 ===
    if (renderSettings) {
        if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable SSAO", &renderSettings->enableSSAO);

            // 质量预设: 0=Low(32 samples), 1=Medium(64), 2=High(128)
            const char* qualityLabels[] = { "Low (32)", "Medium (64)", "High (128)" };
            int quality = renderSettings->ssaoQuality;
            if (ImGui::Combo("Quality", &quality, qualityLabels, IM_ARRAYSIZE(qualityLabels))) {
                renderSettings->ssaoQuality = quality;
            }
        }

        // === Nanite (Cluster Vis) 设置 ===
        if (ImGui::CollapsingHeader("Nanite (Cluster Vis)")) {
            ImGui::Checkbox("GPU LOD Selection", &renderSettings->naniteLODSelection);
            ImGui::Checkbox("Frustum Culling (Z)", &renderSettings->naniteFrustumCulling);
            ImGui::Checkbox("Cone Culling (X)", &renderSettings->naniteConeCulling);
            ImGui::SliderInt("Force LOD (B, -1=off)", &renderSettings->naniteForceLOD, -1, 7);
            ImGui::SliderFloat("Error Threshold (px)", &renderSettings->naniteErrorThreshold, 0.1f, 20.0f, "%.2f");
            ImGui::SliderFloat("Error Scale", &renderSettings->naniteErrorScale, 1.0f, 1000.0f, "%.0f");
            ImGui::TextDisabled("Press 9 to toggle cluster vis, 0 to cycle mode");
        }
    }

    ImGui::Spacing();

    // === Nanite 绘制统计（Cluster Vis 激活时跟随实际绘制）===
    if (naniteTotal > 0) {
        if (ImGui::CollapsingHeader("Nanite Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Clusters: %u total / %u visible / %u drawn",
                        naniteTotal, naniteVisible, naniteDrawn);
            ImGui::Text("Drawn: %u tris, %u verts", naniteTriangles, naniteVertices);

            char lodText[192] = { 0 };
            size_t used = 0;
            for (int lod = 0; lod < NANITE_MAX_LOD; ++lod) {
                if (naniteLodCounts[lod] == 0) continue;
                used += snprintf(lodText + used, sizeof(lodText) - used,
                                 "[L%d:%u] ", lod, naniteLodCounts[lod]);
                if (used >= sizeof(lodText)) break;
            }
            if (lodText[0] != '\0') ImGui::Text("LOD: %s", lodText);
        }
        ImGui::Spacing();
    }

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
