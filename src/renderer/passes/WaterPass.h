#pragma once

#include "RenderPassBase.h"
#include "RenderContext.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class VulkanDevice;
class GBufferPass;
class Mesh;
class RHIDevice;
class RHIBuffer;
class RHIPipeline;
class RHIBindingLayout;

namespace VulkanEngine {
    class Entity;
    struct GPUMesh;
}

/**
 * WaterPass - 水面渲染通道（内置SSR）(RHI)
 * 
 * 直接对水面mesh 进行 SSR 光线步进，只计算水面覆盖的像素
 * 比全屏SSR 后处理效率更高
 * 
 * 注意：WaterPass 不拥有自己的 RenderPass/Framebuffer，
 * 它渲染到外部提供的 RenderPass（通常是 SwapChain 的 final pass）
 */
class WaterPass : public RenderPassBase {
public:
    // 水面参数结构 - 包含 SSR 所需的矩阵
    struct WaterUBO {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::mat4 invView;
        alignas(16) glm::mat4 invProjection;
        alignas(16) glm::vec4 cameraPos;
        alignas(16) glm::vec4 waterColor;     // RGB: 水的颜色, A: 透明度
        alignas(16) glm::vec4 waterParams;    // x: 波浪速度, y: 波浪强度, z: 时间, w: 折射强度
        alignas(16) glm::vec4 screenSize;     // xy: 屏幕尺寸, zw: nearPlane, farPlane
        alignas(16) glm::vec4 ssrParams;      // x: maxDistance, y: maxSteps, z: thickness（线性深度空间）, w: reserved
    };

    WaterPass(std::shared_ptr<VulkanDevice> device, RHIDevice* rhiDevice,
              uint32_t width, uint32_t height, VkRenderPass externalRenderPass);
    ~WaterPass();

    // 禁止拷贝
    WaterPass(const WaterPass&) = delete;
    WaterPass& operator=(const WaterPass&) = delete;

    // 重新调整大小
    void resize(uint32_t width, uint32_t height);

    // 设置水面参数
    void setWaterColor(const glm::vec3& color, float alpha = 0.6f);
    void setWaveSpeed(float speed) { waveSpeed = speed; }
    void setWaveStrength(float strength) { waveStrength = strength; }
    void setRefractionStrength(float strength) { refractionStrength = strength; }
    void setWaterHeight(float height) { waterHeight = height; }
    
    // SSR 参数设置
    void setSSRMaxDistance(float distance) { ssrMaxDistance = distance; }
    void setSSRMaxSteps(float steps) { ssrMaxSteps = steps; }
    void setSSRThickness(float thickness) { ssrThickness = thickness; }

    // 更新 Uniform Buffer
    void updateUniforms(const glm::mat4& view, const glm::mat4& projection,
                        const glm::vec3& cameraPos, float time, uint32_t frameIndex);

    // 更新描述符集 - 需要G-Buffer 用于 SSR
    void updateDescriptorSets(GBufferPass* gbuffer, VkImageView sceneColorView, VkSampler sampler);

    // 渲染水面
    void render(VkCommandBuffer cmd, uint32_t frameIndex);

    // 获取水面高度
    float getWaterHeight() const { return waterHeight; }
    
    // 获取水面网格
    Mesh* getWaterMesh() const { return waterMesh.get(); }
    
    bool setWaterEntity(const VulkanEngine::Entity& entity);
    bool setWaterMesh(std::shared_ptr<VulkanEngine::GPUMesh> gpuMesh);
    void clearExternalMesh();
    bool isUsingExternalMesh() const { return useExternalMesh; }

private:
    void createWaterMesh();
    void createVertexAndIndexBuffers();
    void createBindingLayout();
    void createPipeline();
    void createUniformBuffers();
    void createDescriptorSets();
    void cleanup();

    // RHI device
    RHIDevice* rhiDevice_ = nullptr;
    std::shared_ptr<VulkanDevice> vulkanDevice_;

    uint32_t width_;
    uint32_t height_;
    VkRenderPass externalRenderPass_ = VK_NULL_HANDLE;  // NOT owned

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    // 水面参数
    glm::vec3 waterColor = glm::vec3(0.0f, 0.3f, 0.5f);
    float waterAlpha = 0.7f;
    float waveSpeed = 1.0f;
    float waveStrength = 0.02f;
    float refractionStrength = 1.0f;
    float waterHeight = 0.0f;
    
    // SSR 参数
    float ssrMaxDistance = 30.0f;
    float ssrMaxSteps = 2048.0f;
    float ssrThickness = 0.03f;

    // 水面网格 (内置)
    std::unique_ptr<Mesh> waterMesh;
    std::unique_ptr<RHIBuffer> vertexBuffer_;
    std::unique_ptr<RHIBuffer> indexBuffer_;
    uint32_t indexCount_ = 0;
    
    // 外部水面网格
    std::shared_ptr<VulkanEngine::GPUMesh> externalMesh;
    bool useExternalMesh = false;

    // RHI resources
    std::unique_ptr<RHIPipeline>       pipeline_;
    std::unique_ptr<RHIBindingLayout>  bindingLayout_;

    // Per-frame UBOs (RHI)
    std::vector<std::unique_ptr<RHIBuffer>> uniformBuffers_;

    // Descriptor sets (native Vulkan — hybrid approach for G-Buffer textures)
    std::vector<VkDescriptorSet> descriptorSets_;
};