
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

    // === 渲染模式 + CPU 软光追 ===
    if (renderSettings) {
        if (ImGui::CollapsingHeader("Render Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* modeLabels[] = { "Forward (Raster)", "Deferred + Water",
                                         "CPU Ray Trace", "GPU Ray Trace (DXR)" };
            int mode = static_cast<int>(renderSettings->renderMode);
            if (ImGui::Combo("Mode", &mode, modeLabels, IM_ARRAYSIZE(modeLabels))) {
                renderSettings->renderMode = static_cast<RenderMode>(mode);
            }
        }

        if (renderSettings->renderMode == RenderMode::GPURayTrace &&
            ImGui::CollapsingHeader("GPU Ray Tracing (DXR)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextUnformatted("ReSTIR DI + 1spp PT + SVGF (DXR)");
            ImGui::SliderInt("Max Bounces", &renderSettings->rtMaxBounces, 1, 8);
            ImGui::Checkbox("Shadows", &renderSettings->rtShadows);
            ImGui::SliderFloat("Sky Intensity", &renderSettings->rtSkyIntensity, 0.0f, 3.0f, "%.2f");
            ImGui::SliderFloat("Exposure", &renderSettings->rtExposure, 0.1f, 4.0f, "%.2f");
            ImGui::SliderFloat("Resolution Scale", &renderSettings->rtResolutionScale, 0.1f, 1.0f, "%.2f");

            ImGui::Separator();
            ImGui::TextUnformatted("ReSTIR (unbiased MIS)");
            ImGui::SliderInt("Candidates M", &renderSettings->rtRestirCandidates, 4, 128);
            ImGui::SliderInt("Spatial Neighbors K", &renderSettings->rtRestirSpatialNeighbors, 0, 8);
            ImGui::Checkbox("Temporal Reuse", &renderSettings->rtRestirTemporal);
            ImGui::Checkbox("Shadow In Target", &renderSettings->rtRestirShadowInTarget);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Visibility in target fn (M shadow rays/frame, more accurate)");
            }
            ImGui::Checkbox("Denoiser (SVGF a-trous)", &renderSettings->rtRestirDenoiser);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Variance-driven joint bilateral filter (3x a-trous)");
            }

            ImGui::Separator();
            ImGui::Text("Frame: %.2f ms (CPU submit)", grtFrameMs);
            ImGui::Text("AS build: %.1f ms (on scene change)", grtAsBuildMs);
            ImGui::Text("Tris: %u   BLAS: %u", grtTriangles, grtMeshes);
            ImGui::Text("Instances: %u   Lights: %u", grtInstances, grtLights);
            ImGui::Text("Bindless textures: %u", grtTextures);
        }

        if (renderSettings->renderMode == RenderMode::RayTrace &&
            ImGui::CollapsingHeader("Ray Tracing", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* algoLabels[] = { "Whitted", "Path Tracing", "PT + ReSTIR" };
            ImGui::Combo("Algorithm", &renderSettings->rtAlgorithm, algoLabels, IM_ARRAYSIZE(algoLabels));
            ImGui::SliderInt("Samples/Frame", &renderSettings->rtSamplesPerFrame, 1, 16);
            ImGui::SliderInt("Max Bounces", &renderSettings->rtMaxBounces, 1, 16);
            ImGui::Checkbox("Shadows", &renderSettings->rtShadows);
            ImGui::SliderFloat("Sky Intensity", &renderSettings->rtSkyIntensity, 0.0f, 3.0f, "%.2f");
            ImGui::SliderFloat("Exposure", &renderSettings->rtExposure, 0.1f, 4.0f, "%.2f");
            ImGui::SliderFloat("Resolution Scale", &renderSettings->rtResolutionScale, 0.1f, 1.0f, "%.2f");

            if (renderSettings->rtAlgorithm == 2) {
                ImGui::Separator();
                ImGui::TextUnformatted("ReSTIR (unbiased MIS)");
                ImGui::SliderInt("Candidates M", &renderSettings->rtRestirCandidates, 4, 128);
                ImGui::SliderInt("Spatial Neighbors K", &renderSettings->rtRestirSpatialNeighbors, 0, 8);
                ImGui::Checkbox("Temporal Reuse", &renderSettings->rtRestirTemporal);
                ImGui::Checkbox("Shadow In Target", &renderSettings->rtRestirShadowInTarget);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Visibility in target fn (M shadow rays/frame, more accurate)");
                }
                ImGui::Checkbox("Denoiser (bilateral)", &renderSettings->rtRestirDenoiser);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("GBuffer-guided a-trous joint bilateral filter");
                }
            }

            ImGui::Separator();
            ImGui::Text("Accumulated: %u spp", rtAccumulatedSpp);
            ImGui::Text("RT Frame: %.1f ms", rtFrameMs);
            ImGui::Text("Tris: %u   BVH nodes: %u", rtTriangles, rtBvhNodes);
            ImGui::Text("Instances: %u   Threads: %u", rtInstances, rtThreads);
            ImGui::TextDisabled("Camera/scene change resets accumulation");
        }
    }

    ImGui::Spacing();

    // === SSAO / FXAA 设置 ===
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
        if (ImGui::CollapsingHeader("Anti-Aliasing")) {
            ImGui::Checkbox("Enable FXAA", &renderSettings->enableFXAA);
        }

        // === 水面反射设置（WaterScene 模式）===
        if (ImGui::CollapsingHeader("Water Reflection")) {
            ImGui::Checkbox("Planar Reflection (off = SSR)", &renderSettings->waterPlanarReflection);
            ImGui::SliderFloat("Distortion", &renderSettings->waterReflectionDistortion, 0.0f, 3.0f, "%.2f");
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
