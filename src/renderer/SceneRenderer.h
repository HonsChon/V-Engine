/**
 * @file SceneRenderer.h
 * @brief Scene Renderer - 调度所有渲染 Pass
 * 
 * RHI 无关的渲染调度器(Vulkan/DX12 双后端)。
 * 管理 Forward/Deferred/SSAO/SSR/Water/Nanite 等所有 Pass 的生命周期和执行。
 */

#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>
#include <set>
#include <unordered_map>
#include <chrono>

#include "RenderSettings.h"

// RHI forward declarations
class RHIDevice;
class RHISwapChain;
class RHIRenderPass;
class RHICommandBuffer;
class RHITexture;
class RHISampler;

// Forward declarations
class Camera;

// Render Pass classes
class GBufferPass;
class LightingPass;
class ForwardPass;
class SSRPass;
class WaterPass;
class SSAOPass;
class GPUDrivenRenderer;
class NaniteDebugPass;
struct GPUInstanceData;

// Nanite
namespace Nanite {
    class NaniteManager;
}

// Scene & ECS
namespace VEngine {
    class Scene;
    class RenderSystem;
}

// UI
class ImGuiLayer;
class UIManager;

/**
 * @brief Render statistics
 */
struct RenderStats {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t vertices = 0;
    float gpuTime = 0.0f;
    float cpuTime = 0.0f;
    uint32_t naniteClusters = 0;
    uint32_t naniteTriangles = 0;
    
    void reset() {
        drawCalls = triangles = vertices = 0;
        gpuTime = cpuTime = 0.0f;
        naniteClusters = naniteTriangles = 0;
    }
};

/**
 * @brief Scene Renderer — 调度所有渲染 Pass
 */
class SceneRenderer {
public:
    SceneRenderer(RHIDevice* device, RHISwapChain* swapChain);
    ~SceneRenderer();

    // Non-copyable
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // ========== 生命周期 ==========
    
    /** 初始化（创建 ForwardPass） */
    void initialize();
    
    /** 清理所有资源 */
    void cleanup();

    // ========== 延迟渲染（按需初始化）==========
    
    /** 初始化延迟渲染资源（GBuffer/Lighting/SSR/Water/SSAO） */
    void initDeferredShading();
    
    /** 清理延迟渲染资源 */
    void cleanupDeferredShading();
    
    bool isDeferredInitialized() const { return m_deferredInitialized; }

    // ========== 命令录制 ==========
    
    /**
     * 在给定 command buffer 上录制完整渲染命令
     * @param cmd 命令缓冲区（已 begin，由 Engine 的帧循环管理会话）
     * @param imageIndex swapchain image index
     * @param frameIndex 帧槽索引（0 or 1）
     */
    void recordCommands(RHICommandBuffer* cmd, uint32_t imageIndex, uint32_t frameIndex);

    /** 更新所有 Uniform（每帧调用一次） */
    void updateUniforms(uint32_t frameIndex);

    /** 总时间（秒），用于水面等时间驱动的动画 */
    void setTotalTime(float t) { m_totalTime = t; }

    // ========== 窗口 resize ==========
    
    void onResize(uint32_t width, uint32_t height);
    void onSwapChainRecreated(RHISwapChain* newSwapChain);

    // ========== GPU Culling ==========
    
    void initGPUDrivenRendering();
    void cleanupGPUDrivenRendering();
    void prepareGPUCullingData();

    // ========== Nanite ==========
    
    void initNanite();
    void cleanupNanite();
    void testNaniteClustering();
    void initNaniteDebugPass();
    void cycleNaniteDebugMode();

    // ========== UI ==========
    
    void updateUI();
    void renderUI(RHICommandBuffer* cmd);

    // ========== 设置 & 状态 ==========

    RenderSettings& getSettings() { return m_settings; }
    const RenderSettings& getSettings() const { return m_settings; }
    const RenderStats& getStats() const { return m_stats; }
    
    void setScene(VEngine::Scene* scene) { m_scene = scene; }
    void setCamera(Camera* camera) { m_camera = camera; }
    void setRenderSystem(VEngine::RenderSystem* rs) { m_renderSystem = rs; }
    void setImGuiLayer(ImGuiLayer* layer) { m_imguiLayer = layer; }
    void setUIManager(UIManager* mgr) { m_uiManager = mgr; }

    // Pass 访问
    ForwardPass* getForwardPass() const { return m_forwardPass.get(); }
    GBufferPass* getGBufferPass() const { return m_gbuffer.get(); }
    LightingPass* getLightingPass() const { return m_lightingPass.get(); }
    Nanite::NaniteManager* getNaniteManager() const { return m_naniteManager.get(); }
    RHIDevice* getRHIDevice() const { return m_rhiDevice; }

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

private:
    // ========== 命令录制子方法 ==========
    void recordForwardCommands(RHICommandBuffer* cmd, uint32_t imageIndex, uint32_t frameIndex);
    void recordDeferredCommands(RHICommandBuffer* cmd, uint32_t imageIndex, uint32_t frameIndex);
    void prepareNaniteCulling(RHICommandBuffer* cmd, uint32_t imageIndex, uint32_t frameIndex);
    void recordNaniteDebugCommands(RHICommandBuffer* cmd, uint32_t frameIndex);

    // ========== 矩阵工具 ==========
    /// GLM 的 perspective 面向 Y-up 的 GL/D3D 惯例;Vulkan 的 NDC Y 向下,需要
    /// 翻转投影的 Y 轴才与画面一致。DX12 直接使用标准矩阵。
    glm::mat4 applyApiYFlip(const glm::mat4& proj) const;

    // ========== 资源创建 ==========
    void createSceneColorImage();
    void cleanupSceneColorImage();

    // ========== 引用（不拥有）==========
    RHIDevice* m_rhiDevice = nullptr;
    RHISwapChain* m_swapChain = nullptr;
    VEngine::Scene* m_scene = nullptr;
    Camera* m_camera = nullptr;
    VEngine::RenderSystem* m_renderSystem = nullptr;
    ImGuiLayer* m_imguiLayer = nullptr;
    UIManager* m_uiManager = nullptr;

    // ========== 拥有的 Pass ==========
    std::unique_ptr<ForwardPass> m_forwardPass;
    std::unique_ptr<GBufferPass> m_gbuffer;
    std::unique_ptr<LightingPass> m_lightingPass;
    std::unique_ptr<SSAOPass> m_ssaoPass;
    std::unique_ptr<SSRPass> m_ssrPass;
    std::unique_ptr<WaterPass> m_waterPass;
    std::unique_ptr<GPUDrivenRenderer> m_gpuDrivenRenderer;
    std::unique_ptr<NaniteDebugPass> m_naniteDebugPass;
    std::unique_ptr<Nanite::NaniteManager> m_naniteManager;

    // ========== 场景颜色纹理（SSR 采样）==========
    std::shared_ptr<RHITexture> m_sceneColorTexture;
    std::shared_ptr<RHISampler> m_sceneColorSampler;

    // ========== 状态 ==========
    RenderSettings m_settings;
    RenderStats m_stats;
    bool m_initialized = false;
    bool m_deferredInitialized = false;
    
    // Nanite 状态
    bool m_naniteInitialized = false;
    std::string m_lastClusterizedMeshPath;

    // 时间
    float m_totalTime = 0.0f;
};