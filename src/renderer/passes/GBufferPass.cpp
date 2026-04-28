#include "GBufferPass.h"
#include "VulkanDevice.h"

// RHI headers
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"

// Vulkan backend headers — for downcast to get native handles (compatibility layer)
#include "VulkanRHIDevice.h"
#include "VulkanRHIPipeline.h"
#include "VulkanRHIDescriptor.h"
#include "VulkanRHIBuffer.h"
#include "VulkanRHITexture.h"
#include "VulkanRHISampler.h"
#include "VulkanRHIRenderPass.h"

#include <stdexcept>
#include <iostream>
#include <cstring>

// =============================================================================
// Constructor / Destructor
// =============================================================================

GBufferPass::GBufferPass(std::shared_ptr<VulkanDevice> device,
                         RHIDevice* rhiDevice,
                         uint32_t width, uint32_t height,
                         uint32_t maxFramesInFlight)
    : RenderPassBase(device, width, height)
    , rhiDevice_(rhiDevice)
    , vulkanDevice_(device)
    , width_(width)
    , height_(height)
    , maxFramesInFlight_(maxFramesInFlight)
{
    passName = "GBuffer Pass";

    createAttachments();
    createRHIRenderPass();
    createRHIFramebuffer();
    createRHISampler();
    createBindingLayouts();
    createPipeline();

    std::cout << "GBufferPass created (RHI): " << width_ << "x" << height_ << std::endl;
}

GBufferPass::~GBufferPass() {
    cleanup();
}

void GBufferPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();

    globalBindingGroups_.clear();
    uniformBuffers_.clear();
    materialDescriptorCache_.clear();
    pipeline_.reset();
    globalLayout_.reset();
    materialLayout_.reset();
    framebuffer_.reset();
    renderPass_.reset();
    sampler_.reset();
    for (auto& tex : attachmentTextures_) tex.reset();
}

void GBufferPass::resize(uint32_t newWidth, uint32_t newHeight) {
    if (newWidth == width_ && newHeight == height_) return;

    if (rhiDevice_) rhiDevice_->waitIdle();

    // Release framebuffer-dependent resources
    framebuffer_.reset();
    renderPass_.reset();
    for (auto& tex : attachmentTextures_) tex.reset();
    sampler_.reset();

    width_ = newWidth;
    height_ = newHeight;

    createAttachments();
    createRHIRenderPass();
    createRHIFramebuffer();
    createRHISampler();

    std::cout << "GBufferPass resized (RHI): " << width_ << "x" << height_ << std::endl;
}

// =============================================================================
// RHI Resource Creation
// =============================================================================

void GBufferPass::createAttachments() {
    // Position - R16G16B16A16_SFLOAT
    {
        RHITextureDesc desc{};
        desc.width = width_;
        desc.height = height_;
        desc.format = RHIFormat::R16G16B16A16_SFLOAT;
        desc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled;
        attachmentTextures_[POSITION] = rhiDevice_->createTexture(desc);
    }

    // Normal - R16G16B16A16_SFLOAT
    {
        RHITextureDesc desc{};
        desc.width = width_;
        desc.height = height_;
        desc.format = RHIFormat::R16G16B16A16_SFLOAT;
        desc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled;
        attachmentTextures_[NORMAL] = rhiDevice_->createTexture(desc);
    }

    // Albedo - R8G8B8A8_UNORM
    {
        RHITextureDesc desc{};
        desc.width = width_;
        desc.height = height_;
        desc.format = RHIFormat::R8G8B8A8_UNORM;
        desc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled;
        attachmentTextures_[ALBEDO] = rhiDevice_->createTexture(desc);
    }

    // Depth - D32_SFLOAT
    {
        RHITextureDesc desc{};
        desc.width = width_;
        desc.height = height_;
        desc.format = RHIFormat::D32_SFLOAT;
        desc.usage = RHITextureUsage::DepthStencilAttachment | RHITextureUsage::Sampled;
        attachmentTextures_[DEPTH] = rhiDevice_->createTexture(desc);
    }
}

void GBufferPass::createRHIRenderPass() {
    RHIRenderPassDesc desc{};

    // Position attachment
    desc.addColorAttachment(
        RHIFormat::R16G16B16A16_SFLOAT,
        RHILoadOp::Clear,
        RHIStoreOp::Store,
        RHIImageLayout::Undefined,
        RHIImageLayout::ShaderReadOnly
    );

    // Normal attachment
    desc.addColorAttachment(
        RHIFormat::R16G16B16A16_SFLOAT,
        RHILoadOp::Clear,
        RHIStoreOp::Store,
        RHIImageLayout::Undefined,
        RHIImageLayout::ShaderReadOnly
    );

    // Albedo attachment
    desc.addColorAttachment(
        RHIFormat::R8G8B8A8_UNORM,
        RHILoadOp::Clear,
        RHIStoreOp::Store,
        RHIImageLayout::Undefined,
        RHIImageLayout::ShaderReadOnly
    );

    // Depth attachment
    desc.setDepthAttachment(
        RHIFormat::D32_SFLOAT,
        RHILoadOp::Clear,
        RHIStoreOp::Store,
        RHIImageLayout::Undefined,
        RHIImageLayout::DepthStencilReadOnly
    );

    renderPass_ = rhiDevice_->createRenderPass(desc);
}

void GBufferPass::createRHIFramebuffer() {
    RHIFramebufferDesc desc{};
    desc.renderPass = renderPass_.get();
    desc.width = width_;
    desc.height = height_;
    for (int i = 0; i < COUNT; ++i) {
        desc.attachments.push_back(attachmentTextures_[i].get());
    }

    framebuffer_ = rhiDevice_->createFramebuffer(desc);
}

void GBufferPass::createRHISampler() {
    RHISamplerDesc desc{};
    desc.magFilter = RHIFilter::Linear;
    desc.minFilter = RHIFilter::Linear;
    desc.addressModeU = RHIAddressMode::ClampToEdge;
    desc.addressModeV = RHIAddressMode::ClampToEdge;
    desc.addressModeW = RHIAddressMode::ClampToEdge;
    desc.anisotropyEnable = false;
    desc.maxAnisotropy = 1.0f;
    desc.mipMapFilter = RHIFilter::Linear;
    desc.minLod = 0.0f;
    desc.maxLod = 1.0f;

    sampler_ = rhiDevice_->createSampler(desc);
}

void GBufferPass::createBindingLayouts() {
    // Set 0: Global UBO
    {
        RHIBindingLayoutDesc desc;
        desc.entries.push_back({
            0,                                  // binding
            RHIDescriptorType::UniformBuffer,   // type
            RHIShaderStage::Vertex | RHIShaderStage::Fragment,  // stageFlags
            1                                   // count
        });
        globalLayout_ = rhiDevice_->createBindingLayout(desc);
    }

    // Set 1: Material Textures (3 combined image samplers)
    {
        RHIBindingLayoutDesc desc;
        for (uint32_t i = 0; i < 3; ++i) {
            desc.entries.push_back({
                i,
                RHIDescriptorType::CombinedImageSampler,
                RHIShaderStage::Fragment,
                1
            });
        }
        materialLayout_ = rhiDevice_->createBindingLayout(desc);
    }

    std::cout << "GBuffer binding layouts created (RHI)" << std::endl;
}

void GBufferPass::createPipeline() {
    constexpr uint32_t vertexStride = sizeof(float) * 11;

    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    auto* vkBuilder = static_cast<VulkanGraphicsPipelineBuilder*>(builder.get());

    vkBuilder
        ->setVertexShader("shaders/gbuffer_vert.spv")
        .setFragmentShader("shaders/gbuffer_frag.spv")
        .addVertexBinding(0, vertexStride, RHIVertexInputRate::Vertex)
        .addVertexAttribute(0, 0, RHIFormat::R32G32B32_SFLOAT, 0)                      // Position
        .addVertexAttribute(0, 1, RHIFormat::R32G32B32_SFLOAT, sizeof(float) * 3)       // Normal
        .addVertexAttribute(0, 2, RHIFormat::R32G32_SFLOAT,    sizeof(float) * 6)       // TexCoord
        .addVertexAttribute(0, 3, RHIFormat::R32G32B32_SFLOAT, sizeof(float) * 8)       // Tangent
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::Back)
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(true, true, RHICompareOp::Less)
        .setSampleCount(RHISampleCount::Count1)
        .setColorAttachmentCount(3)  // GBuffer has 3 color attachments
        .addBindingLayout(globalLayout_.get())
        .addBindingLayout(materialLayout_.get())
        .addPushConstant(RHIShaderStage::Vertex, 0, sizeof(PushConstantData))
        .setRenderPass(renderPass_.get());

    pipeline_ = vkBuilder->build();

    std::cout << "GBuffer pipeline created (RHI Pipeline Builder)" << std::endl;
}

void GBufferPass::createUniformBuffers() {
    uniformBuffers_.resize(maxFramesInFlight_);

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        RHIBufferDesc desc{};
        desc.size = sizeof(UniformBufferObject);
        desc.usage = RHIBufferUsage::Uniform;
        desc.memoryUsage = RHIMemoryUsage::CPUToGPU;
        uniformBuffers_[i] = rhiDevice_->createBuffer(desc);
    }
}

void GBufferPass::createGlobalBindingGroups() {
    globalBindingGroups_.resize(maxFramesInFlight_);

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        RHIBindingGroupDesc desc;
        RHIBindingGroupEntry entry{};
        entry.binding = 0;
        entry.kind = RHIBindingGroupEntry::Kind::Buffer;
        entry.bufferBinding.buffer = uniformBuffers_[i].get();
        entry.bufferBinding.offset = 0;
        entry.bufferBinding.range = sizeof(UniformBufferObject);
        desc.entries.push_back(entry);

        globalBindingGroups_[i] = rhiDevice_->createBindingGroup(globalLayout_.get(), desc);
    }

    std::cout << "GBuffer global binding groups created (RHI)" << std::endl;
}

// =============================================================================
// Descriptor Sets initialization (called externally)
// =============================================================================

void GBufferPass::createDescriptorSets() {
    createUniformBuffers();
    createGlobalBindingGroups();
    std::cout << "GBuffer descriptor sets initialized via RHI" << std::endl;
}

// =============================================================================
// UBO Update
// =============================================================================

void GBufferPass::updateUniformBuffer(uint32_t frameIndex, const UniformBufferObject& ubo) {
    if (frameIndex < uniformBuffers_.size()) {
        void* ptr = uniformBuffers_[frameIndex]->map();
        memcpy(ptr, &ubo, sizeof(ubo));
        uniformBuffers_[frameIndex]->unmap();
    }
}

// =============================================================================
// Material Descriptor Management
// =============================================================================

GBufferPass::MaterialDescriptor* GBufferPass::allocateMaterialDescriptor(const std::string& materialId) {
    auto it = materialDescriptorCache_.find(materialId);
    if (it != materialDescriptorCache_.end()) {
        return &it->second;
    }

    MaterialDescriptor descriptor;
    descriptor.nativeSets.resize(maxFramesInFlight_, VK_NULL_HANDLE);
    descriptor.valid = false;

    materialDescriptorCache_[materialId] = std::move(descriptor);

    std::cout << "GBuffer: Allocated material descriptor (RHI): " << materialId << std::endl;
    return &materialDescriptorCache_[materialId];
}

GBufferPass::MaterialDescriptor* GBufferPass::getMaterialDescriptor(const std::string& materialId) {
    auto it = materialDescriptorCache_.find(materialId);
    if (it != materialDescriptorCache_.end() && it->second.valid) {
        return &it->second;
    }
    return nullptr;
}

void GBufferPass::updateMaterialTextures(MaterialDescriptor* material,
                                          VkImageView albedoView, VkSampler albedoSampler,
                                          VkImageView normalView, VkSampler normalSampler,
                                          VkImageView specularView, VkSampler specularSampler) {
    if (!material) return;

    // COMPATIBILITY: Material system still provides raw Vulkan handles.
    auto* vkLayout = static_cast<VulkanRHIBindingLayout*>(materialLayout_.get());
    auto* vkDevice = static_cast<VulkanRHIDevice*>(rhiDevice_);

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        VkDescriptorSet ds = vkDevice->allocateDescriptorSet(vkLayout->getVkDescriptorSetLayout());

        std::array<VkDescriptorImageInfo, 3> imageInfos{};
        imageInfos[0] = { albedoSampler,   albedoView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        imageInfos[1] = { normalSampler,   normalView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        imageInfos[2] = { specularSampler, specularView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        std::array<VkWriteDescriptorSet, 3> writes{};
        for (int j = 0; j < 3; ++j) {
            writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[j].dstSet = ds;
            writes[j].dstBinding = j;
            writes[j].dstArrayElement = 0;
            writes[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[j].descriptorCount = 1;
            writes[j].pImageInfo = &imageInfos[j];
        }

        vkUpdateDescriptorSets(vkDevice->getVkDevice(),
                               static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);

        material->nativeSets[i] = ds;
    }

    material->valid = true;
}

// =============================================================================
// Native Handle Accessors (compatibility — transitional)
// =============================================================================

VkRenderPass GBufferPass::getRenderPass() const {
    if (!renderPass_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHIRenderPass*>(renderPass_.get())->getVkRenderPass();
}

VkFramebuffer GBufferPass::getFramebuffer() const {
    if (!framebuffer_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHIFramebuffer*>(framebuffer_.get())->getVkFramebuffer();
}

VkImageView GBufferPass::getPositionView() const {
    if (!attachmentTextures_[POSITION]) return VK_NULL_HANDLE;
    return static_cast<VulkanRHITexture*>(attachmentTextures_[POSITION].get())->getVkImageView();
}

VkImageView GBufferPass::getNormalView() const {
    if (!attachmentTextures_[NORMAL]) return VK_NULL_HANDLE;
    return static_cast<VulkanRHITexture*>(attachmentTextures_[NORMAL].get())->getVkImageView();
}

VkImageView GBufferPass::getAlbedoView() const {
    if (!attachmentTextures_[ALBEDO]) return VK_NULL_HANDLE;
    return static_cast<VulkanRHITexture*>(attachmentTextures_[ALBEDO].get())->getVkImageView();
}

VkImageView GBufferPass::getDepthView() const {
    if (!attachmentTextures_[DEPTH]) return VK_NULL_HANDLE;
    return static_cast<VulkanRHITexture*>(attachmentTextures_[DEPTH].get())->getVkImageView();
}

VkImage GBufferPass::getPositionImage() const {
    if (!attachmentTextures_[POSITION]) return VK_NULL_HANDLE;
    return static_cast<VulkanRHITexture*>(attachmentTextures_[POSITION].get())->getVkImage();
}

VkImage GBufferPass::getNormalImage() const {
    if (!attachmentTextures_[NORMAL]) return VK_NULL_HANDLE;
    return static_cast<VulkanRHITexture*>(attachmentTextures_[NORMAL].get())->getVkImage();
}

VkImage GBufferPass::getAlbedoImage() const {
    if (!attachmentTextures_[ALBEDO]) return VK_NULL_HANDLE;
    return static_cast<VulkanRHITexture*>(attachmentTextures_[ALBEDO].get())->getVkImage();
}

VkImage GBufferPass::getDepthImage() const {
    if (!attachmentTextures_[DEPTH]) return VK_NULL_HANDLE;
    return static_cast<VulkanRHITexture*>(attachmentTextures_[DEPTH].get())->getVkImage();
}

VkSampler GBufferPass::getSampler() const {
    if (!sampler_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHISampler*>(sampler_.get())->getVkSampler();
}

VkPipeline GBufferPass::getPipeline() const {
    if (!pipeline_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHIPipeline*>(pipeline_.get())->getVkPipeline();
}

VkPipelineLayout GBufferPass::getPipelineLayout() const {
    if (!pipeline_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHIPipeline*>(pipeline_.get())->getVkPipelineLayout();
}

// =============================================================================
// Render Commands (VkCommandBuffer compatibility — transitional)
// =============================================================================

void GBufferPass::beginRenderPass(VkCommandBuffer cmd) {
    auto clearValues = getClearValues();

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = getRenderPass();
    renderPassInfo.framebuffer = getFramebuffer();
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = { width_, height_ };
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width_);
    viewport.height = static_cast<float>(height_);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = { width_, height_ };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void GBufferPass::endRenderPass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

std::array<VkClearValue, 4> GBufferPass::getClearValues() const {
    std::array<VkClearValue, 4> clearValues{};
    clearValues[0].color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};     // Position
    clearValues[1].color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};     // Normal
    clearValues[2].color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};     // Albedo
    clearValues[3].depthStencil = { 1.0f, 0 };                 // Depth
    return clearValues;
}

void GBufferPass::bindPipeline(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getPipeline());
}

void GBufferPass::bindGlobalDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex) const {
    if (frameIndex < globalBindingGroups_.size() && globalBindingGroups_[frameIndex]) {
        auto* vkGroup = static_cast<VulkanRHIBindingGroup*>(globalBindingGroups_[frameIndex].get());
        VkDescriptorSet ds = vkGroup->getVkDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getPipelineLayout(),
                                0, 1, &ds, 0, nullptr);
    }
}

void GBufferPass::bindMaterialDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex,
                                             MaterialDescriptor* material) const {
    if (material && material->valid && frameIndex < material->nativeSets.size()) {
        VkDescriptorSet ds = material->nativeSets[frameIndex];
        if (ds != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getPipelineLayout(),
                                    1, 1, &ds, 0, nullptr);
        }
    }
}

void GBufferPass::drawMesh(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkBuffer indexBuffer,
                            uint32_t indexCount) const {
    if (vertexBuffer == VK_NULL_HANDLE || indexBuffer == VK_NULL_HANDLE) return;

    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}

void GBufferPass::pushModelMatrix(VkCommandBuffer cmd, const glm::mat4& model) {
    PushConstantData pushData{};
    pushData.model = model;
    pushData.normalMatrix = glm::transpose(glm::inverse(model));

    vkCmdPushConstants(cmd, getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(PushConstantData), &pushData);
}

// =============================================================================
// recordCommands
// =============================================================================

void GBufferPass::recordCommands(VkCommandBuffer cmd, uint32_t frameIndex) {
    recordCommands(cmd, currentContext);
}

void GBufferPass::recordCommands(VkCommandBuffer cmd, const RenderContext& context) {
    if (!enabled) return;

    beginRenderPass(cmd);
    bindPipeline(cmd);

    if (context.sceneVertexBuffer != VK_NULL_HANDLE &&
        context.sceneIndexBuffer != VK_NULL_HANDLE &&
        context.sceneIndexCount > 0) {

        VkBuffer vertexBuffers[] = { context.sceneVertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, context.sceneIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, context.sceneIndexCount, 1, 0, 0, 0);
    }

    endRenderPass(cmd);
}