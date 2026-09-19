#pragma once

#include "RenderPassBase.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

// Pure RHI forward declarations
class GBufferPass;
class Mesh;
class RHIDevice;
class RHIBuffer;
class RHIPipeline;
class RHIBindingLayout;
class RHIBindingGroup;
class RHIRenderPass;
class RHICommandBuffer;
class RHITexture;
class RHISampler;

namespace VEngine {
    class Entity;
    struct GPUMesh;
}

/**
 * WaterPass - 水面渲染通道 (Pure RHI)
 */
class WaterPass : public RenderPassBase {
public:
    struct WaterUBO {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::mat4 invView;
        alignas(16) glm::mat4 invProjection;
        alignas(16) glm::vec4 cameraPos;
        alignas(16) glm::vec4 waterColor;
        alignas(16) glm::vec4 waterParams;
        alignas(16) glm::vec4 screenSize;
        alignas(16) glm::vec4 ssrParams;
        alignas(16) glm::vec4 reflectionParams;   // x: 0=SSR / 1=平面反射, y: 反射波纹扰动强度
    };

    WaterPass(RHIDevice* rhiDevice,
              uint32_t width, uint32_t height, RHIRenderPass* externalRenderPass);
    ~WaterPass();

    WaterPass(const WaterPass&) = delete;
    WaterPass& operator=(const WaterPass&) = delete;

    void resize(uint32_t width, uint32_t height);

    void setWaterColor(const glm::vec3& color, float alpha = 0.6f);
    void setWaveSpeed(float speed) { waveSpeed = speed; }
    void setWaveStrength(float strength) { waveStrength = strength; }
    void setRefractionStrength(float strength) { refractionStrength = strength; }
    void setWaterHeight(float height) { waterHeight = height; }
    void setSSRMaxDistance(float distance) { ssrMaxDistance = distance; }
    void setSSRMaxSteps(float steps) { ssrMaxSteps = steps; }
    void setSSRThickness(float thickness) { ssrThickness = thickness; }

    // 平面反射：模式（1=平面反射 RT 采样 / 0=SSR 步进）与波纹扰动强度
    void setReflectionParams(float mode, float distortion) {
        reflectionMode = mode; reflectionDistortion = distortion;
    }

    void updateUniforms(const glm::mat4& view, const glm::mat4& projection,
                        const glm::vec3& cameraPos, float time, uint32_t frameIndex);

    // Set G-Buffer inputs (Pure RHI)
    void setGBufferInputs(GBufferPass* gbuffer, RHITexture* sceneColorTexture, RHISampler* sampler);

    // Set planar reflection RT (binding 5, 由 ReflectionPass 提供)
    void setReflectionTexture(RHITexture* reflectionTexture, RHISampler* sampler);

    // Render (Pure RHI)
    void render(RHICommandBuffer* cmd, uint32_t frameIndex);

    float getWaterHeight() const { return waterHeight; }
    Mesh* getWaterMesh() const { return waterMesh.get(); }

private:
    void createWaterMesh();
    void createVertexAndIndexBuffers();
    void createBindingLayout();
    void createPipeline();
    void createUniformBuffers();
    void createBindingGroups();
    void cleanup();

    RHIDevice* rhiDevice_ = nullptr;
    uint32_t width_;
    uint32_t height_;
    RHIRenderPass* externalRenderPass_ = nullptr;

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    glm::vec3 waterColor = glm::vec3(0.0f, 0.3f, 0.5f);
    float waterAlpha = 0.7f;
    float waveSpeed = 1.0f;
    float waveStrength = 0.02f;
    float refractionStrength = 1.0f;
    float waterHeight = 0.0f;
    float ssrMaxDistance = 30.0f;
    float ssrMaxSteps = 2048.0f;
    float ssrThickness = 0.05f;   // 线性深度空间厚度窗口；穿越由深度连续性守卫+二分细化兜底，小窗口可减少掠过后方的误命中
    float reflectionMode = 1.0f;        // 1=平面反射（默认），0=SSR
    float reflectionDistortion = 1.0f;  // 平面反射的波纹扰动强度

    std::unique_ptr<Mesh> waterMesh;
    std::shared_ptr<RHIBuffer> vertexBuffer_;
    std::shared_ptr<RHIBuffer> indexBuffer_;
    uint32_t indexCount_ = 0;

    std::shared_ptr<RHIPipeline>       pipeline_;
    std::shared_ptr<RHIBindingLayout>  bindingLayout_;
    std::vector<std::shared_ptr<RHIBuffer>> uniformBuffers_;
    std::vector<std::shared_ptr<RHIBindingGroup>> bindingGroups_;
};