/**
 * @file RenderSettings.h
 * @brief 渲染设置 — 独立头文件，供 Engine、SceneRenderer、UI 共用
 */

#pragma once

#include <cstdint>

/**
 * 渲染模式
 */
enum class RenderMode {
    Normal,      // 前向渲染 (Forward PBR)
    WaterScene   // 延迟渲染 + SSR + Water
};

/**
 * 渲染设置（由 UI 面板读写，由渲染器每帧读取）
 */
struct RenderSettings {
    // 渲染模式
    RenderMode renderMode = RenderMode::Normal;

    // Feature toggles
    bool enableSSR = true;
    bool enableSSAO = true;
    bool enableWater = false;
    bool enableNanite = true;
    bool enableGPUCulling = true;

    // Debug options
    bool showClusterVisualization = false;
    int clusterDebugMode = 0;  // 0: Off, 1: LOD, 2: Cluster ID

    // Quality settings
    int shadowQuality = 2;     // 0: Off, 1: Low, 2: Medium, 3: High
    int ssaoQuality = 1;       // 0: Low(32), 1: Medium(64), 2: High(128)
    float renderScale = 1.0f;

    // UI
    bool showUI = true;
};
