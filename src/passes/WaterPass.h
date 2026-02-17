#pragma once

#include "RenderPassBase.h"
#include "RenderContext.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class VulkanDevice;
class VulkanBuffer;
class Mesh;
class GBufferPass;

namespace VulkanEngine {
    class Entity;
    struct GPUMesh;
}

/**
 * WaterPass - 水面渲染通道（内置 SSR）
 * 
 * 直接对水面 mesh 进行 SSR 光线步进，只计算水面覆盖的像素
 * 比全屏 SSR 后处理效率更高
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

    WaterPass(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height,
              VkRenderPass renderPass);
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

    // 更新描述符集 - 需要 G-Buffer 用于 SSR
    void updateDescriptorSets(GBufferPass* gbuffer, VkImageView sceneColorView, VkSampler sampler);

    // 渲染水面
    void render(VkCommandBuffer cmd, uint32_t frameIndex);

    // 获取水面高度
    float getWaterHeight() const { return waterHeight; }
    
    // 获取水面网格
    Mesh* getWaterMesh() const { return waterMesh.get(); }
    
    /**
     * @brief 设置水面 Entity
     * 从给定的 Entity 获取其 MeshRendererComponent，然后使用其 mesh 作为水面
     * @param entity 要作为水面的实体
     * @return true 如果成功设置，false 如果 entity 无效或没有 mesh
     */
    bool setWaterEntity(const VulkanEngine::Entity& entity);
    
    /**
     * @brief 设置水面网格 (通过 GPUMesh)
     * 直接使用已有的 GPUMesh 作为水面
     * @param gpuMesh 指向 GPUMesh 的共享指针
     * @return true 如果成功设置，false 如果 gpuMesh 无效
     */
    bool setWaterMesh(std::shared_ptr<VulkanEngine::GPUMesh> gpuMesh);
    
    /**
     * @brief 清除外部设置的水面网格，恢复使用内置网格
     */
    void clearExternalMesh();
    
    /**
     * @brief 检查是否正在使用外部网格
     */
    bool isUsingExternalMesh() const { return useExternalMesh; }

private:
    void createWaterMesh();
    void createVertexBuffer();
    void createIndexBuffer();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createPipeline();
    void createUniformBuffers();
    void cleanup();

    std::shared_ptr<VulkanDevice> device;
    
    uint32_t width;
    uint32_t height;
    VkRenderPass renderPass;

    // 水面参数
    glm::vec3 waterColor = glm::vec3(0.0f, 0.3f, 0.5f);
    float waterAlpha = 0.7f;
    float waveSpeed = 1.0f;
    float waveStrength = 0.02f;
    float refractionStrength = 1.0f;
    float waterHeight = 0.0f;
    
    // SSR 参数
    float ssrMaxDistance = 30.0f;   // 最大光线步进距离（世界单位）
    float ssrMaxSteps = 256.0f;     // 最大步进次数
    float ssrThickness = 0.05f;     // 厚度阈值（线性深度空间，世界单位）

    // 水面网格 (内置)
    std::unique_ptr<Mesh> waterMesh;
    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;
    
    // 外部水面网格
    std::shared_ptr<VulkanEngine::GPUMesh> externalMesh;
    bool useExternalMesh = false;

    // Vulkan 资源
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    
    // 描述符
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    
    // Uniform Buffers
    std::vector<std::unique_ptr<VulkanBuffer>> uniformBuffers;
    std::vector<void*> uniformBuffersMapped;
    
    static const int MAX_FRAMES_IN_FLIGHT = 2;
};