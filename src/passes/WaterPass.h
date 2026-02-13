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

/**
 * WaterPass - 水面渲染通道
 * 
 * 渲染带有 SSR 反射的水面
 */
class WaterPass : public RenderPassBase {
public:
    // 水面参数结构
    struct WaterUBO {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::vec4 cameraPos;
        alignas(16) glm::vec4 waterColor;     // RGB: 水的颜色, A: 透明度
        alignas(16) glm::vec4 waterParams;    // x: 波浪速度, y: 波浪强度, z: 时间, w: 折射强度
        alignas(16) glm::vec4 screenSize;     // xy: 屏幕尺寸
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

    // 更新 Uniform Buffer
    void updateUniforms(const glm::mat4& view, const glm::mat4& projection,
                        const glm::vec3& cameraPos, float time, uint32_t frameIndex);

    // 更新描述符集（设置反射纹理等）
    void updateDescriptorSets(VkImageView reflectionView, VkImageView depthView,
                              VkImageView sceneColorView, VkSampler sampler);

    // 渲染水面
    void render(VkCommandBuffer cmd, uint32_t frameIndex);

    // 获取水面高度
    float getWaterHeight() const { return waterHeight; }
    
    // 获取水面网格
    Mesh* getWaterMesh() const { return waterMesh.get(); }

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

    // 水面网格
    std::unique_ptr<Mesh> waterMesh;
    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;

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
