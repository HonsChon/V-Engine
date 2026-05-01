#include "GBufferPass.h"

// RHI headers (Pure RHI — no backend includes)
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"
#include "RHICommandBuffer.h"

#include <stdexcept>
#include <iostream>
#include <cstring>

// =============================================================================
// Constructor / Destructor
// =============================================================================

GBufferPass::GBufferPass(RHIDevice* rhiDevice,
                         uint32_t width, uint32_t height,
                         uint32_t maxFramesInFlight)
    : RenderPassBase(rhiDevice, width, height)
    , rhiDevice_(rhiDevice)
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
        desc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled | RHITextureUsage::TransferSrc;
        attachmentTextures_[POSITION] = rhiDevice_->createTexture(desc);
    }

    // Normal - R16G16B16A16_SFLOAT
    {
        RHITextureDesc desc{};
        desc.width = width_;
        desc.height = height_;
        desc.format = RHIFormat::R16G16B16A16_SFLOAT;
        desc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled | RHITextureUsage::TransferSrc;
        attachmentTextures_[NORMAL] = rhiDevice_->createTexture(desc);
    }

    // Albedo - R8G8B8A8_UNORM
    {
        RHITextureDesc desc{};
        desc.width = width_;
        desc.height = height_;
        desc.format = RHIFormat::R8G8B8A8_UNORM;
        desc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled | RHITextureUsage::TransferSrc;
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

    builder->setVertexShader("shaders/gbuffer_vert.spv")
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

    pipeline_ = builder->build();

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
    descriptor.groups.resize(maxFramesInFlight_);
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        descriptor.groups[i] = rhiDevice_->allocateBindingGroup(materialLayout_.get());
    }
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

// =============================================================================
// Render Commands (Pure RHI — RHICommandBuffer)
// =============================================================================

void GBufferPass::beginRenderPass(RHICommandBuffer* cmd) {
    std::vector<RHIClearValue> clearValues(4);
    clearValues[0].color = { 0.0f, 0.0f, 0.0f, 0.0f };     // Position
    clearValues[1].color = { 0.0f, 0.0f, 0.0f, 0.0f };     // Normal
    clearValues[2].color = { 0.0f, 0.0f, 0.0f, 0.0f };     // Albedo
    clearValues[3].depthStencil = { 1.0f, 0 };               // Depth

    cmd->beginRenderPass(renderPass_.get(), framebuffer_.get(), clearValues);
    cmd->setViewport(0, 0, static_cast<float>(width_), static_cast<float>(height_));
    cmd->setScissor(0, 0, width_, height_);
}

void GBufferPass::endRenderPass(RHICommandBuffer* cmd) {
    cmd->endRenderPass();
}

void GBufferPass::bindPipeline(RHICommandBuffer* cmd) const {
    cmd->bindGraphicsPipeline(pipeline_.get());
}

void GBufferPass::bindGlobalDescriptorSet(RHICommandBuffer* cmd, uint32_t frameIndex) const {
    if (frameIndex < globalBindingGroups_.size() && globalBindingGroups_[frameIndex]) {
        cmd->setBindingGroup(0, globalBindingGroups_[frameIndex].get());
    }
}

void GBufferPass::bindMaterialDescriptorSet(RHICommandBuffer* cmd, uint32_t frameIndex,
                                             MaterialDescriptor* material) const {
    if (!material || !material->valid || frameIndex >= material->groups.size()) return;
    if (material->groups[frameIndex]) {
        cmd->setBindingGroup(1, material->groups[frameIndex].get());
    }
}

void GBufferPass::drawMesh(RHICommandBuffer* cmd, RHIBuffer* vertexBuffer, RHIBuffer* indexBuffer,
                            uint32_t indexCount) const {
    if (!vertexBuffer || !indexBuffer) return;
    cmd->bindVertexBuffer(0, vertexBuffer);
    cmd->bindIndexBuffer(indexBuffer, 0, RHIIndexType::UInt32);
    cmd->drawIndexed(indexCount, 1, 0, 0, 0);
}

void GBufferPass::pushModelMatrix(RHICommandBuffer* cmd, const glm::mat4& model) {
    PushConstantData pushData{};
    pushData.model = model;
    pushData.normalMatrix = glm::transpose(glm::inverse(model));
    cmd->pushConstants(RHIShaderStage::Vertex, 0, sizeof(PushConstantData), &pushData);
}
