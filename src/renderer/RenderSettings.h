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
    Normal,       // 前向渲染 (Forward PBR)
    WaterScene,   // 延迟渲染 + SSR + Water
    RayTrace,     // CPU 软光追（Whitted / 路径追踪，渐进累积）
    GPURayTrace   // GPU 硬件光追（DXR：RT-GBuffer + ReSTIR + 1spp PT + SVGF）
};

/**
 * 渲染设置（由 UI 面板读写，由渲染器每帧读取）
 */
struct RenderSettings {
    // 渲染模式
    RenderMode renderMode = RenderMode::Normal;

    // ========== GPU 硬件光追（RenderMode::GPURayTrace，DX12/DXR）==========
    // 管线固定为 ReSTIR DI + 1spp PT 间接光 + SVGF（对齐 CPU 版 rtAlgorithm==2）
    // 复用字段：rtMaxBounces / rtShadows / rtSkyIntensity / rtResolutionScale /
    //          rtExposure / rtRestirCandidates / rtRestirSpatialNeighbors /
    //          rtRestirTemporal / rtRestirShadowInTarget / rtRestirDenoiser
    bool rtGpuShowDebugView = false;   // 预留：GBuffer 调试视图

    // ========== CPU 软光追（RenderMode::RayTrace）==========
    int rtAlgorithm = 0;            // 0=Whitted, 1=路径追踪, 2=PT+ReSTIR
    int rtSamplesPerFrame = 1;      // 每帧每像素样本数（相机静止时跨帧累积）
    int rtMaxBounces = 5;           // 最大弹射次数
    bool rtShadows = true;          // 阴影射线
    float rtSkyIntensity = 1.0f;    // 天光强度
    float rtResolutionScale = 0.5f; // 内部分辨率比例（1.0 = 全分辨率）
    float rtExposure = 1.0f;        // 曝光（tone map 前线性乘）

    // ReSTIR（rtAlgorithm == 2）
    int rtRestirCandidates = 32;        // 初始候选数 M
    int rtRestirSpatialNeighbors = 3;   // 空间复用邻居数 K
    bool rtRestirTemporal = true;       // 时间复用
    bool rtRestirShadowInTarget = false;// 可见性计入目标函数（贵但阴影更准）
    bool rtRestirDenoiser = true;       // GBuffer 引导双边滤波降噪

    // Feature toggles
    bool enableSSR = true;
    bool enableSSAO = true;
    bool enableWater = false;
    bool enableNanite = true;
    bool enableGPUCulling = true;
    bool enableFXAA = true;      // FXAA 后处理抗锯齿（关闭时直通）

    // 水面反射（WaterScene 模式）
    bool waterPlanarReflection = true;      // true=平面反射 RT / false=SSR 步进
    float waterReflectionDistortion = 1.0f; // 平面反射波纹扰动强度

    // Debug options
    bool showClusterVisualization = false;

    // Nanite cluster LOD tuning (applied to NaniteConfig each frame)
    bool naniteLODSelection = true;
    bool naniteFrustumCulling = true;
    bool naniteConeCulling = true;
    int naniteForceLOD = -1;             // -1=关闭; 0..7=强制显示该 LOD (诊断)
    float naniteErrorThreshold = 1.0f;   // 屏幕误差阈值（像素）
    float naniteErrorScale = 300.0f;     // QEM 误差放大系数（小模型需较大值）

    // Quality settings
    int shadowQuality = 2;     // 0: Off, 1: Low, 2: Medium, 3: High
    int ssaoQuality = 1;       // 0: Low(32), 1: Medium(64), 2: High(128)
    float renderScale = 1.0f;

    // UI
    bool showUI = true;
};
