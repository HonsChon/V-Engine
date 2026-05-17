#include "VulkanRHIPipeline.h"
#include "VulkanRHIDevice.h"
#include "VulkanRHIShader.h"
#include "VulkanRHIDescriptor.h"
#include "VulkanRHIRenderPass.h"
#include "VulkanTypeConversions.h"
#include <stdexcept>
#include <array>

using namespace VulkanTypeConversions;

// =============================================================================
// VulkanRHIPipeline
// =============================================================================

VulkanRHIPipeline::VulkanRHIPipeline(VulkanRHIDevice* device, VkPipeline pipeline,
                                     VkPipelineLayout layout, RHIPipelineType type)
    : device_(device), pipeline_(pipeline), pipelineLayout_(layout), type_(type) {}

VulkanRHIPipeline::~VulkanRHIPipeline() {
    VkDevice vkDev = device_->getVkDevice();
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(vkDev, pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(vkDev, pipelineLayout_, nullptr);
}

// =============================================================================
// VulkanGraphicsPipelineBuilder
// =============================================================================

VulkanGraphicsPipelineBuilder::VulkanGraphicsPipelineBuilder(VulkanRHIDevice* device)
    : device_(device) {}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setVertexShader(const std::string& path) {
    vertShaderPath_ = path;
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setFragmentShader(const std::string& path) {
    fragShaderPath_ = path;
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::addVertexBinding(
    uint32_t binding, uint32_t stride, RHIVertexInputRate inputRate) {
    VkVertexInputBindingDescription desc{};
    desc.binding = binding;
    desc.stride = stride;
    desc.inputRate = toVkVertexInputRate(inputRate);
    vertexBindings_.push_back(desc);
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::addVertexAttribute(
    uint32_t binding, uint32_t location, RHIFormat format, uint32_t offset) {
    VkVertexInputAttributeDescription desc{};
    desc.binding = binding;
    desc.location = location;
    desc.format = toVkFormat(format);
    desc.offset = offset;
    vertexAttributes_.push_back(desc);
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setTopology(RHIPrimitiveTopology topology) {
    topology_ = toVkTopology(topology);
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setCullMode(RHICullMode mode) {
    cullMode_ = toVkCullMode(mode);
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setFrontFace(RHIFrontFace face) {
    frontFace_ = toVkFrontFace(face);
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setPolygonMode(RHIPolygonMode mode) {
    polygonMode_ = toVkPolygonMode(mode);
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setLineWidth(float width) {
    lineWidth_ = width;
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setDepthBias(
    bool enable, float constantFactor, float slopeFactor, float clamp) {
    depthBiasEnable_ = enable;
    depthBiasConstant_ = constantFactor;
    depthBiasSlope_ = slopeFactor;
    depthBiasClamp_ = clamp;
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setDepthTest(
    bool enable, bool writeEnable, RHICompareOp compareOp) {
    depthTestEnable_ = enable;
    depthWriteEnable_ = writeEnable;
    depthCompareOp_ = toVkCompareOp(compareOp);
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setStencilTest(bool enable) {
    stencilTestEnable_ = enable;
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setSampleCount(RHISampleCount count) {
    sampleCount_ = toVkSampleCount(count);
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::addColorBlendAttachment(
    const RHIColorBlendAttachment& attachment) {
    VkPipelineColorBlendAttachmentState state{};
    state.blendEnable = attachment.blendEnable ? VK_TRUE : VK_FALSE;
    state.srcColorBlendFactor = toVkBlendFactor(attachment.srcColorFactor);
    state.dstColorBlendFactor = toVkBlendFactor(attachment.dstColorFactor);
    state.colorBlendOp = toVkBlendOp(attachment.colorBlendOp);
    state.srcAlphaBlendFactor = toVkBlendFactor(attachment.srcAlphaFactor);
    state.dstAlphaBlendFactor = toVkBlendFactor(attachment.dstAlphaFactor);
    state.alphaBlendOp = toVkBlendOp(attachment.alphaBlendOp);
    state.colorWriteMask = toVkColorComponent(attachment.colorWriteMask);
    colorBlendAttachments_.push_back(state);
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setColorAttachmentCount(uint32_t count) {
    // Pre-fill with default (no blend, write all) if not enough attachments exist
    VkPipelineColorBlendAttachmentState defaultBlend{};
    defaultBlend.blendEnable = VK_FALSE;
    defaultBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments_.resize(count, defaultBlend);
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::addDynamicState(RHIDynamicState state) {
    dynamicStates_.push_back(toVkDynamicState(state));
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::addBindingLayout(RHIBindingLayout* layout) {
    auto* vkLayout = static_cast<VulkanRHIBindingLayout*>(layout);
    descriptorSetLayouts_.push_back(vkLayout->getVkDescriptorSetLayout());
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::addPushConstant(
    RHIShaderStage stages, uint32_t offset, uint32_t size) {
    VkPushConstantRange range{};
    range.stageFlags = toVkShaderStage(stages);
    range.offset = offset;
    range.size = size;
    pushConstantRanges_.push_back(range);
    return *this;
}

RHIGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setRenderPass(
    RHIRenderPass* renderPass, uint32_t subpass) {
    if (renderPass) {
        auto* vkRP = static_cast<VulkanRHIRenderPass*>(renderPass);
        renderPass_ = vkRP->getVkRenderPass();
    }
    subpass_ = subpass;
    return *this;
}

VulkanGraphicsPipelineBuilder& VulkanGraphicsPipelineBuilder::setNativeRenderPass(
    VkRenderPass renderPass, uint32_t subpass) {
    renderPass_ = renderPass;
    subpass_ = subpass;
    return *this;
}

std::shared_ptr<RHIPipeline> VulkanGraphicsPipelineBuilder::build() {
    VkDevice vkDev = device_->getVkDevice();

    // ---- Create shader modules ----
    VulkanRHIShader vertShader(device_, RHIShaderStage::Vertex, vertShaderPath_);
    VulkanRHIShader fragShader(device_, RHIShaderStage::Fragment, fragShaderPath_);

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShader.getVkShaderModule();
    shaderStages[0].pName = vertShader.getEntryPoint().c_str();

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShader.getVkShaderModule();
    shaderStages[1].pName = fragShader.getEntryPoint().c_str();

    // ---- Vertex input state ----
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings_.size());
    vertexInputInfo.pVertexBindingDescriptions = vertexBindings_.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes_.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes_.data();

    // ---- Input assembly ----
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = topology_;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // ---- Viewport & scissor (dynamic by default) ----
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // ---- Rasterization ----
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = polygonMode_;
    rasterizer.lineWidth = lineWidth_;
    rasterizer.cullMode = cullMode_;
    rasterizer.frontFace = frontFace_;
    rasterizer.depthBiasEnable = depthBiasEnable_ ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = depthBiasConstant_;
    rasterizer.depthBiasSlopeFactor = depthBiasSlope_;
    rasterizer.depthBiasClamp = depthBiasClamp_;

    // ---- Multisampling ----
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = sampleCount_;
    multisampling.sampleShadingEnable = VK_FALSE;

    // ---- Depth / stencil ----
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = depthTestEnable_ ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = depthWriteEnable_ ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = depthCompareOp_;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = stencilTestEnable_ ? VK_TRUE : VK_FALSE;

    // ---- Color blending ----
    // If user didn't add any, provide a default (no blend, write all)
    if (colorBlendAttachments_.empty()) {
        VkPipelineColorBlendAttachmentState defaultBlend{};
        defaultBlend.blendEnable = VK_FALSE;
        defaultBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments_.push_back(defaultBlend);
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments_.size());
    colorBlending.pAttachments = colorBlendAttachments_.data();

    // ---- Dynamic state ----
    // Always include viewport and scissor if not already present
    auto hasDynState = [&](VkDynamicState s) {
        for (auto ds : dynamicStates_) if (ds == s) return true;
        return false;
    };
    if (!hasDynState(VK_DYNAMIC_STATE_VIEWPORT)) dynamicStates_.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    if (!hasDynState(VK_DYNAMIC_STATE_SCISSOR))  dynamicStates_.push_back(VK_DYNAMIC_STATE_SCISSOR);

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates_.size());
    dynamicState.pDynamicStates = dynamicStates_.data();

    // ---- Pipeline layout ----
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts_.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts_.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges_.size());
    pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges_.data();

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(vkDev, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanGraphicsPipelineBuilder] Failed to create pipeline layout!");
    }

    // ---- Create graphics pipeline ----
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = subpass_;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(vkDev, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(vkDev, pipelineLayout, nullptr);
        throw std::runtime_error("[VulkanGraphicsPipelineBuilder] Failed to create graphics pipeline!");
    }

    return std::make_shared<VulkanRHIPipeline>(device_, pipeline, pipelineLayout,
                                               RHIPipelineType::Graphics);
}

// =============================================================================
// VulkanComputePipelineBuilder
// =============================================================================

VulkanComputePipelineBuilder::VulkanComputePipelineBuilder(VulkanRHIDevice* device)
    : device_(device) {}

RHIComputePipelineBuilder& VulkanComputePipelineBuilder::setComputeShader(const std::string& path) {
    computeShaderPath_ = path;
    return *this;
}

RHIComputePipelineBuilder& VulkanComputePipelineBuilder::addBindingLayout(RHIBindingLayout* layout) {
    auto* vkLayout = static_cast<VulkanRHIBindingLayout*>(layout);
    descriptorSetLayouts_.push_back(vkLayout->getVkDescriptorSetLayout());
    return *this;
}

RHIComputePipelineBuilder& VulkanComputePipelineBuilder::addPushConstant(
    RHIShaderStage stages, uint32_t offset, uint32_t size) {
    VkPushConstantRange range{};
    range.stageFlags = toVkShaderStage(stages);
    range.offset = offset;
    range.size = size;
    pushConstantRanges_.push_back(range);
    return *this;
}

std::shared_ptr<RHIPipeline> VulkanComputePipelineBuilder::build() {
    VkDevice vkDev = device_->getVkDevice();

    VulkanRHIShader computeShader(device_, RHIShaderStage::Compute, computeShaderPath_);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = computeShader.getVkShaderModule();
    stageInfo.pName = computeShader.getEntryPoint().c_str();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts_.size());
    layoutInfo.pSetLayouts = descriptorSetLayouts_.data();
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges_.size());
    layoutInfo.pPushConstantRanges = pushConstantRanges_.data();

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(vkDev, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanComputePipelineBuilder] Failed to create pipeline layout!");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout;

    VkPipeline pipeline;
    if (vkCreateComputePipelines(vkDev, VK_NULL_HANDLE, 1, &pipelineInfo,
                                 nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(vkDev, pipelineLayout, nullptr);
        throw std::runtime_error("[VulkanComputePipelineBuilder] Failed to create compute pipeline!");
    }

    return std::make_shared<VulkanRHIPipeline>(device_, pipeline, pipelineLayout,
                                               RHIPipelineType::Compute);
}
