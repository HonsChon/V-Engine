#include "ForwardPass.h"

// Pure RHI headers — NO Vulkan backend headers
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"
#include "RHICommandBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"

#include <stdexcept>
#include <iostream>
#include <cstring>

// =============================================================================
// Constructor / Destructor
// =============================================================================

ForwardPass::ForwardPass(RHIDevice* rhiDevice,
                         RHIRenderPass* renderPass,
                         uint32_t width, uint32_t height,
                         uint32_t maxFramesInFlight)
    : RenderPassBase(rhiDevice, width, height)
    , rhiDevice_(rhiDevice)
    , renderPass_(renderPass)
    , width_(width)
    , height_(height)
    , maxFramesInFlight_(maxFramesInFlight)
{
    passName = "Forward Pass";
    createBindingLayouts();
    createPipeline();
    createUniformBuffers();
    createGlobalBindingGroups();
    std::cout << "[ForwardPass] Created (Pure RHI): " << width_ << "x" << height_ << std::endl;
}

ForwardPass::~ForwardPass() { cleanup(); }

void ForwardPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();
    globalBindingGroups_.clear();
    uniformBuffers_.clear();
    materialDescriptorCache_.clear();
    pipeline_.reset();
    globalLayout_.reset();
    materialLayout_.reset();
}

void ForwardPass::recreate(RHIRenderPass* newRenderPass, uint32_t newWidth, uint32_t newHeight) {
    if (rhiDevice_) rhiDevice_->waitIdle();
    pipeline_.reset();
    renderPass_ = newRenderPass;
    width_ = newWidth;
    height_ = newHeight;
    createPipeline();
}

// =============================================================================
// RHI Resource Creation
// =============================================================================

void ForwardPass::createBindingLayouts() {
    // Set 0: Global UBO
    {
        RHIBindingLayoutDesc desc;
        desc.entries.push_back({0, RHIDescriptorType::UniformBuffer,
                                RHIShaderStage::Vertex | RHIShaderStage::Fragment, 1});
        globalLayout_ = rhiDevice_->createBindingLayout(desc);
    }
    // Set 1: Material Textures (3 combined image samplers)
    {
        RHIBindingLayoutDesc desc;
        for (uint32_t i = 0; i < 3; ++i) {
            desc.entries.push_back({i, RHIDescriptorType::CombinedImageSampler,
                                    RHIShaderStage::Fragment, 1});
        }
        materialLayout_ = rhiDevice_->createBindingLayout(desc);
    }
}

void ForwardPass::createPipeline() {
    constexpr uint32_t vertexStride = sizeof(float) * 11;

    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    builder->setVertexShader("shaders/pbr_vert.spv")
        .setFragmentShader("shaders/pbr_frag.spv")
        .addVertexBinding(0, vertexStride, RHIVertexInputRate::Vertex)
        .addVertexAttribute(0, 0, RHIFormat::R32G32B32_SFLOAT, 0)
        .addVertexAttribute(0, 1, RHIFormat::R32G32B32_SFLOAT, sizeof(float) * 3)
        .addVertexAttribute(0, 2, RHIFormat::R32G32_SFLOAT,    sizeof(float) * 6)
        .addVertexAttribute(0, 3, RHIFormat::R32G32B32_SFLOAT, sizeof(float) * 8)
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::Back)
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(true, true, RHICompareOp::Less)
        .setSampleCount(RHISampleCount::Count1)
        .addBindingLayout(globalLayout_.get())
        .addBindingLayout(materialLayout_.get())
        .addPushConstant(RHIShaderStage::Vertex, 0, sizeof(PushConstantData))
        .setRenderPass(renderPass_);

    pipeline_ = builder->build();
    std::cout << "[ForwardPass] Pipeline created (Pure RHI)" << std::endl;
}

void ForwardPass::createUniformBuffers() {
    uniformBuffers_.resize(maxFramesInFlight_);
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        RHIBufferDesc d{}; d.size = sizeof(UniformBufferObject);
        d.usage = RHIBufferUsage::Uniform; d.memoryUsage = RHIMemoryUsage::CPUToGPU;
        uniformBuffers_[i] = rhiDevice_->createBuffer(d);
    }
}

void ForwardPass::createGlobalBindingGroups() {
    globalBindingGroups_.resize(maxFramesInFlight_);
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        globalBindingGroups_[i] = rhiDevice_->allocateBindingGroup(globalLayout_.get());
        globalBindingGroups_[i]->updateBuffer(0, uniformBuffers_[i].get(), 0, sizeof(UniformBufferObject));
    }
}

// =============================================================================
// UBO Update
// =============================================================================

void ForwardPass::updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo) {
    void* ptr = uniformBuffers_[currentFrame]->map();
    memcpy(ptr, &ubo, sizeof(ubo));
    uniformBuffers_[currentFrame]->unmap();
}

// =============================================================================
// Material Descriptor Management (Pure RHI)
// =============================================================================

ForwardPass::MaterialDescriptor* ForwardPass::allocateMaterialDescriptor(const std::string& materialId) {
    auto it = materialDescriptorCache_.find(materialId);
    if (it != materialDescriptorCache_.end()) return &it->second;

    MaterialDescriptor descriptor;
    descriptor.groups.resize(maxFramesInFlight_);
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        descriptor.groups[i] = rhiDevice_->allocateBindingGroup(materialLayout_.get());
    }
    descriptor.valid = false;
    materialDescriptorCache_[materialId] = std::move(descriptor);
    return &materialDescriptorCache_[materialId];
}

void ForwardPass::updateMaterialTextures(MaterialDescriptor* material,
                                          RHITexture* albedoTex, RHISampler* albedoSampler,
                                          RHITexture* normalTex, RHISampler* normalSampler,
                                          RHITexture* specularTex, RHISampler* specularSampler) {
    if (!material) return;
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        if (!material->groups[i]) continue;
        material->groups[i]->updateTexture(0, albedoTex, albedoSampler);
        material->groups[i]->updateTexture(1, normalTex, normalSampler);
        material->groups[i]->updateTexture(2, specularTex, specularSampler);
    }
    material->valid = true;
}

ForwardPass::MaterialDescriptor* ForwardPass::getMaterialDescriptor(const std::string& materialId) {
    auto it = materialDescriptorCache_.find(materialId);
    if (it != materialDescriptorCache_.end() && it->second.valid) return &it->second;
    return nullptr;
}

// =============================================================================
// Render Commands (Pure RHI)
// =============================================================================

void ForwardPass::begin(RHICommandBuffer* cmd) {
    cmd->setViewport(0, 0, float(width_), float(height_));
    cmd->setScissor(0, 0, width_, height_);
}

void ForwardPass::bindPipeline(RHICommandBuffer* cmd) {
    cmd->bindGraphicsPipeline(pipeline_.get());
}

void ForwardPass::bindGlobalDescriptorSet(RHICommandBuffer* cmd, uint32_t frameIndex) {
    cmd->setBindingGroup(0, globalBindingGroups_[frameIndex].get());
}

void ForwardPass::bindMaterialDescriptorSet(RHICommandBuffer* cmd, uint32_t frameIndex,
                                             MaterialDescriptor* material) {
    if (!material || !material->valid || frameIndex >= material->groups.size()) return;
    if (material->groups[frameIndex]) {
        cmd->setBindingGroup(1, material->groups[frameIndex].get());
    }
}

void ForwardPass::pushModelMatrix(RHICommandBuffer* cmd, const glm::mat4& model) {
    PushConstantData pushData{};
    pushData.model = model;
    pushData.normalMatrix = glm::transpose(glm::inverse(model));
    cmd->pushConstants(RHIShaderStage::Vertex, 0, sizeof(PushConstantData), &pushData);
}

void ForwardPass::drawMesh(RHICommandBuffer* cmd, RHIBuffer* vertexBuffer, RHIBuffer* indexBuffer,
                            uint32_t indexCount) {
    if (!vertexBuffer || !indexBuffer) return;
    cmd->bindVertexBuffer(0, vertexBuffer);
    cmd->bindIndexBuffer(indexBuffer, 0, RHIIndexType::UInt32);
    cmd->drawIndexed(indexCount, 1, 0, 0, 0);
}