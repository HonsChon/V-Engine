#pragma once

#include "FrustumCullingPass.h"
#include <memory>
#include <vector>

class RHIDevice;
class RHICommandBuffer;
class RHIBuffer;
class Camera;

/**
 * GPUDrivenRenderer - GPU 驱动渲染管理器 (Pure RHI)
 */
struct GPUDrivenRendererConfig {
    uint32_t maxInstances = 100000;
    bool enableFrustumCulling = true;
    bool enableOcclusionCulling = false;
    bool enableLODSelection = true;
};

struct GPUDrivenRendererStatistics {
    uint32_t totalInstances = 0;
    uint32_t visibleInstances = 0;
    uint32_t culledInstances = 0;
    float cullingTimeMs = 0.0f;
};

class GPUDrivenRenderer {
public:
    using Config = GPUDrivenRendererConfig;
    using Statistics = GPUDrivenRendererStatistics;

    GPUDrivenRenderer(RHIDevice* rhiDevice, const Config& config = Config{});
    ~GPUDrivenRenderer();

    void init();
    void prepare(const std::vector<GPUInstanceData>& instances,
                 const glm::mat4& viewMatrix,
                 const glm::mat4& projMatrix,
                 const glm::vec3& cameraPos);

    void executeCulling(RHICommandBuffer* cmd);

    RHIBuffer* getIndirectDrawBuffer() const;
    RHIBuffer* getVisibleIndicesBuffer() const;
    uint32_t getVisibleCount() const;
    const std::vector<uint32_t>& getVisibleIndices();

    const Statistics& getStatistics() const { return stats; }
    bool isEnabled() const { return config.enableFrustumCulling; }
    void setEnabled(bool enabled) { config.enableFrustumCulling = enabled; }

private:
    RHIDevice* rhiDevice_ = nullptr;
    Config config;
    Statistics stats;

    std::unique_ptr<FrustumCullingPass> frustumCullingPass;
};