#pragma once

#include "RHIPipeline.h"
#include <vulkan/vulkan.h>
#include <vector>

class VulkanRHIDevice;

// =============================================================================
// VulkanRHIPipeline — wraps VkPipeline + VkPipelineLayout
// =============================================================================

class VulkanRHIPipeline : public RHIPipeline {
public:
    VulkanRHIPipeline(VulkanRHIDevice* device, VkPipeline pipeline,
                      VkPipelineLayout layout, RHIPipelineType type);
    ~VulkanRHIPipeline() override;

    RHIPipelineType getType() const override { return type_; }

    VkPipeline       getVkPipeline() const { return pipeline_; }
    VkPipelineLayout getVkPipelineLayout() const { return pipelineLayout_; }

private:
    VulkanRHIDevice* device_;
    VkPipeline       pipeline_       = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    RHIPipelineType  type_;
};

// =============================================================================
// VulkanGraphicsPipelineBuilder
// =============================================================================

class VulkanGraphicsPipelineBuilder : public RHIGraphicsPipelineBuilder {
public:
    explicit VulkanGraphicsPipelineBuilder(VulkanRHIDevice* device);
    ~VulkanGraphicsPipelineBuilder() override = default;

    RHIGraphicsPipelineBuilder& setVertexShader(const std::string& path) override;
    RHIGraphicsPipelineBuilder& setFragmentShader(const std::string& path) override;

    RHIGraphicsPipelineBuilder& addVertexBinding(uint32_t binding, uint32_t stride,
                                                   RHIVertexInputRate inputRate) override;
    RHIGraphicsPipelineBuilder& addVertexAttribute(uint32_t binding, uint32_t location,
                                                     RHIFormat format, uint32_t offset) override;

    RHIGraphicsPipelineBuilder& setTopology(RHIPrimitiveTopology topology) override;

    RHIGraphicsPipelineBuilder& setCullMode(RHICullMode mode) override;
    RHIGraphicsPipelineBuilder& setFrontFace(RHIFrontFace face) override;
    RHIGraphicsPipelineBuilder& setPolygonMode(RHIPolygonMode mode) override;
    RHIGraphicsPipelineBuilder& setLineWidth(float width) override;
    RHIGraphicsPipelineBuilder& setDepthBias(bool enable, float constantFactor,
                                               float slopeFactor, float clamp) override;

    RHIGraphicsPipelineBuilder& setDepthTest(bool enable, bool writeEnable,
                                               RHICompareOp compareOp) override;
    RHIGraphicsPipelineBuilder& setStencilTest(bool enable) override;

    RHIGraphicsPipelineBuilder& setSampleCount(RHISampleCount count) override;
    RHIGraphicsPipelineBuilder& addColorBlendAttachment(const RHIColorBlendAttachment& attachment) override;
    RHIGraphicsPipelineBuilder& setColorAttachmentCount(uint32_t count) override;
    RHIGraphicsPipelineBuilder& addDynamicState(RHIDynamicState state) override;
    RHIGraphicsPipelineBuilder& addBindingLayout(RHIBindingLayout* layout) override;
    RHIGraphicsPipelineBuilder& addPushConstant(RHIShaderStage stages, uint32_t offset, uint32_t size) override;
    RHIGraphicsPipelineBuilder& setRenderPass(RHIRenderPass* renderPass, uint32_t subpass) override;

    // Transitional: set native VkRenderPass directly (for compatibility during migration)
    VulkanGraphicsPipelineBuilder& setNativeRenderPass(VkRenderPass renderPass, uint32_t subpass = 0);

    std::unique_ptr<RHIPipeline> build() override;

private:
    VulkanRHIDevice* device_;

    // Shader paths
    std::string vertShaderPath_;
    std::string fragShaderPath_;

    // Vertex input
    std::vector<VkVertexInputBindingDescription>   vertexBindings_;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes_;

    // Input assembly
    VkPrimitiveTopology topology_ = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Rasterization
    VkCullModeFlags  cullMode_    = VK_CULL_MODE_BACK_BIT;
    VkFrontFace      frontFace_   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode    polygonMode_ = VK_POLYGON_MODE_FILL;
    float            lineWidth_   = 1.0f;
    bool             depthBiasEnable_    = false;
    float            depthBiasConstant_  = 0.0f;
    float            depthBiasSlope_     = 0.0f;
    float            depthBiasClamp_     = 0.0f;

    // Depth / stencil
    bool         depthTestEnable_  = true;
    bool         depthWriteEnable_ = true;
    VkCompareOp  depthCompareOp_   = VK_COMPARE_OP_LESS;
    bool         stencilTestEnable_ = false;

    // Multisampling
    VkSampleCountFlagBits sampleCount_ = VK_SAMPLE_COUNT_1_BIT;

    // Color blend
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments_;

    // Dynamic states
    std::vector<VkDynamicState> dynamicStates_;

    // Layouts
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts_;
    std::vector<VkPushConstantRange>   pushConstantRanges_;

    // Render pass
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    uint32_t     subpass_    = 0;
};

// =============================================================================
// VulkanComputePipelineBuilder
// =============================================================================

class VulkanComputePipelineBuilder : public RHIComputePipelineBuilder {
public:
    explicit VulkanComputePipelineBuilder(VulkanRHIDevice* device);
    ~VulkanComputePipelineBuilder() override = default;

    RHIComputePipelineBuilder& setComputeShader(const std::string& path) override;
    RHIComputePipelineBuilder& addBindingLayout(RHIBindingLayout* layout) override;
    RHIComputePipelineBuilder& addPushConstant(RHIShaderStage stages, uint32_t offset, uint32_t size) override;

    std::unique_ptr<RHIPipeline> build() override;

private:
    VulkanRHIDevice* device_;
    std::string computeShaderPath_;
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts_;
    std::vector<VkPushConstantRange>   pushConstantRanges_;
};
