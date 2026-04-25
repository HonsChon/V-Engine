/**
 * @file SceneRenderer.h
 * @brief Scene Renderer - responsible for actual scene rendering
 * 
 * Responsibilities:
 * 1. Manage render passes (GBuffer, Lighting, Forward, etc.)
 * 2. Coordinate rendering order
 * 3. Handle render-related Uniform updates
 * 4. Manage render targets
 * 
 * Design Philosophy:
 * - High-level render coordinator, doesn't directly handle Vulkan commands
 * - Each Pass is independent, SceneRenderer schedules their execution order
 * - Prepared for future Render Graph implementation
 */

#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include "RenderContext.h"

// Forward declarations
class VulkanDevice;
class VulkanSwapChain;
class Camera;
class FrameResources;

// Render Pass classes
class GBufferPass;
class LightingPass;
class ForwardPass;
class SSRPass;
class WaterPass;
class NaniteDebugPass;
class SSAOPass;

// Scene related
namespace VulkanEngine {
    class Scene;
}

/**
 * @brief Render statistics
 */
struct RenderStats {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t vertices = 0;
    float gpuTime = 0.0f;      // GPU time (milliseconds)
    float cpuTime = 0.0f;      // CPU render preparation time
    
    // Nanite related
    uint32_t naniteClusters = 0;
    uint32_t naniteTriangles = 0;
    
    void reset() {
        drawCalls = triangles = vertices = 0;
        gpuTime = cpuTime = 0.0f;
        naniteClusters = naniteTriangles = 0;
    }
};

/**
 * @brief Render settings
 */
struct RenderSettings {
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
    int ssaoQuality = 1;
    float renderScale = 1.0f;
};

/**
 * @brief Scene Renderer
 */
class SceneRenderer {
public:
    SceneRenderer(VulkanDevice* device, VulkanSwapChain* swapChain);
    ~SceneRenderer();

    // Non-copyable
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    /**
     * @brief Initialize all render passes
     */
    void initialize();

    /**
     * @brief Render a frame
     * @param context Render context
     */
    void render(const RenderContext& context);

    /**
     * @brief Handle window resize
     */
    void onResize(uint32_t width, uint32_t height);

    /**
     * @brief Handle swap chain recreation
     */
    void onSwapChainRecreated(VulkanSwapChain* newSwapChain);

    /**
     * @brief Cleanup resources
     */
    void cleanup();

    // ========== Settings ==========

    RenderSettings& getSettings() { return m_settings; }
    const RenderSettings& getSettings() const { return m_settings; }

    void setScene(VulkanEngine::Scene* scene) { m_scene = scene; }
    void setCamera(Camera* camera) { m_camera = camera; }

    // ========== Statistics ==========

    const RenderStats& getStats() const { return m_stats; }

    // ========== Pass access (for UI debugging, etc.) ==========

    GBufferPass* getGBufferPass() const { return m_gBufferPass.get(); }
    LightingPass* getLightingPass() const { return m_lightingPass.get(); }
    ForwardPass* getForwardPass() const { return m_forwardPass.get(); }

private:
    // Render stages
    void executeGBufferPass(const RenderContext& context);
    void executeSSAOPass(const RenderContext& context);
    void executeLightingPass(const RenderContext& context);
    void executeForwardPass(const RenderContext& context);
    void executeSSRPass(const RenderContext& context);
    void executeWaterPass(const RenderContext& context);
    void executeNanitePass(const RenderContext& context);
    void executeDebugPass(const RenderContext& context);

    // Create render passes
    void createPasses();
    void destroyPasses();

    // Device references
    VulkanDevice* m_device = nullptr;
    VulkanSwapChain* m_swapChain = nullptr;

    // Scene and camera references
    VulkanEngine::Scene* m_scene = nullptr;
    Camera* m_camera = nullptr;

    // Render Passes
    std::unique_ptr<GBufferPass> m_gBufferPass;
    std::unique_ptr<LightingPass> m_lightingPass;
    std::unique_ptr<ForwardPass> m_forwardPass;
    std::unique_ptr<SSRPass> m_ssrPass;
    std::unique_ptr<WaterPass> m_waterPass;
    std::unique_ptr<NaniteDebugPass> m_naniteDebugPass;
    std::unique_ptr<SSAOPass> m_ssaoPass;

    // Settings and stats
    RenderSettings m_settings;
    RenderStats m_stats;

    bool m_initialized = false;
    
    // Constants
    static const int MAX_FRAMES_IN_FLIGHT = 2;
};