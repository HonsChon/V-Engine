#pragma once

#include "RenderPassBase.h"
#include "RenderContext.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <array>
#include <string>
#include <unordered_map>

class VulkanDevice;
class VulkanBuffer;

/**
 * GBufferPass - 几何缓冲区渲染通道
 * 
 * 用于延迟渲染的第一阶段，存储场景的几何信息：
 * - Position (RGB16F) - 世界空间位置
 * - Normal (RGB16F) - 世界空间法线
 * - Albedo (RGBA8) - 反照率 + 金属度
 * - Depth (D32F) - 深度缓冲
 * 
 * 描述符集架构：
 * - Set 0: 全局 UBO（view, proj, 光照）
 * - Set 1: 材质纹理（albedo, normal, specular）- 每个材质独立
 */
class GBufferPass : public RenderPassBase {
public:
    // G-Buffer 附件索引
    enum Attachment {
        POSITION = 0,
        NORMAL = 1,
        ALBEDO = 2,
        DEPTH = 3,
        COUNT = 4
    };

    // Push Constants 结构体
    struct PushConstantData {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 normalMatrix;
    };
    
    // 材质描述符结构体
    struct MaterialDescriptor {
        std::vector<VkDescriptorSet> sets;  // 每帧一个
        bool valid = false;
    };

    // UBO 结构体
    struct UniformBufferObject {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec4 viewPos;
        alignas(16) glm::vec4 lightPos;
        alignas(16) glm::vec4 lightColor;
    };

    GBufferPass(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height);
    ~GBufferPass();

    GBufferPass(const GBufferPass&) = delete;
    GBufferPass& operator=(const GBufferPass&) = delete;

    void resize(uint32_t width, uint32_t height);

    // 获取器
    VkRenderPass getRenderPass() const { return renderPass; }
    VkFramebuffer getFramebuffer() const { return framebuffer; }
    
    VkImageView getPositionView() const { return attachmentViews[POSITION]; }
    VkImageView getNormalView() const { return attachmentViews[NORMAL]; }
    VkImageView getAlbedoView() const { return attachmentViews[ALBEDO]; }
    VkImageView getDepthView() const { return attachmentViews[DEPTH]; }
    
    VkImage getPositionImage() const { return attachmentImages[POSITION]; }
    VkImage getNormalImage() const { return attachmentImages[NORMAL]; }
    VkImage getAlbedoImage() const { return attachmentImages[ALBEDO]; }
    VkImage getDepthImage() const { return attachmentImages[DEPTH]; }
    
    VkSampler getSampler() const { return sampler; }
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }

    // RenderPass 控制
    void beginRenderPass(VkCommandBuffer cmd);
    void endRenderPass(VkCommandBuffer cmd);
    std::array<VkClearValue, 4> getClearValues() const;

    // Pipeline 相关
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    void bindPipeline(VkCommandBuffer cmd) const;
    
    // 描述符绑定
    void bindGlobalDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex) const;
    void bindMaterialDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex, MaterialDescriptor* material) const;
    
    // 绘制
    void drawMesh(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkBuffer indexBuffer, uint32_t indexCount) const;
    void pushModelMatrix(VkCommandBuffer cmd, const glm::mat4& model);
    
    // 初始化描述符
    void createDescriptorSets();
    
    // 材质描述符管理
    MaterialDescriptor* allocateMaterialDescriptor(const std::string& materialId);
    MaterialDescriptor* getMaterialDescriptor(const std::string& materialId);
    void updateMaterialTextures(MaterialDescriptor* material,
                                VkImageView albedoView, VkSampler albedoSampler,
                                VkImageView normalView, VkSampler normalSampler,
                                VkImageView specularView, VkSampler specularSampler);
    
    // UBO 更新
    void updateUniformBuffer(uint32_t frameIndex, const UniformBufferObject& ubo);

    // 基类接口实现
    void recordCommands(VkCommandBuffer cmd, uint32_t frameIndex) override;
    void recordCommands(VkCommandBuffer cmd, const RenderContext& context);

private:
    void createAttachments();
    void createRenderPass();
    void createFramebuffer();
    void createSampler();
    void cleanup();
    void createImage(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t index);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    
    void createDescriptorSetLayout();
    void createPipeline();
    void createUniformBuffers();
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);

    std::shared_ptr<VulkanDevice> device;
    uint32_t width;
    uint32_t height;

    // 附件资源
    std::array<VkImage, COUNT> attachmentImages = {};
    std::array<VkDeviceMemory, COUNT> attachmentMemories = {};
    std::array<VkImageView, COUNT> attachmentViews = {};
    std::array<VkFormat, COUNT> attachmentFormats = {
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_D32_SFLOAT
    };

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    
    // Pipeline
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    
    // 描述符集布局
    VkDescriptorSetLayout globalSetLayout = VK_NULL_HANDLE;    // Set 0: UBO
    VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;  // Set 1: 纹理
    
    // 描述符资源
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    static constexpr uint32_t MAX_MATERIALS = 100;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    
    // 全局描述符集 (Set 0)
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> globalDescriptorSets = {};
    
    // 材质描述符缓存 (Set 1)
    std::unordered_map<std::string, MaterialDescriptor> materialDescriptorCache;
    
    // Uniform Buffers
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> uniformBuffers = {};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> uniformBuffersMemory = {};
    std::array<void*, MAX_FRAMES_IN_FLIGHT> uniformBuffersMapped = {};
    
    VkDescriptorSet currentDescriptorSet = VK_NULL_HANDLE;
    RenderContext currentContext;
};