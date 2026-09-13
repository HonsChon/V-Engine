/**
 * @file TransparentPass.cpp
 * @brief 延迟模式半透明前向绘制（采样 GBuffer 深度做手动剔除）
 */

#include "TransparentPass.h"

#include "ForwardPass.h"
#include "Mesh.h"
#include "RenderSystem.h"
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"
#include "RHICommandBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"

#include <cstring>
#include <iostream>

TransparentPass::TransparentPass(RHIDevice* rhiDevice,
                                 RHIRenderPass* renderPass,
                                 RHIBindingLayout* materialLayout,
                                 uint32_t width, uint32_t height,
                                 uint32_t maxFramesInFlight)
    : RenderPassBase(rhiDevice, width, height)
    , rhiDevice_(rhiDevice)
    , renderPass_(renderPass)
    , materialLayout_(materialLayout)
    , width_(width)
    , height_(height)
    , maxFramesInFlight_(maxFramesInFlight)
{
    passName = "Transparent Pass";
    createLayouts();
    createPipeline();
    createUniformBuffers();
    createGlobalBindingGroups();
    std::cout << "[TransparentPass] Created (Pure RHI): " << width_ << "x" << height_ << std::endl;
}

TransparentPass::~TransparentPass() { cleanup(); }

void TransparentPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();
    globalBindingGroups_.clear();
    uniformBuffers_.clear();
    pipeline_.reset();
    globalLayout_.reset();
    depthSampler_.reset();
}

void TransparentPass::recreate(RHIRenderPass* newRenderPass, uint32_t newWidth, uint32_t newHeight) {
    if (rhiDevice_) rhiDevice_->waitIdle();
    pipeline_.reset();
    renderPass_ = newRenderPass;
    width_ = newWidth;
    height_ = newHeight;
    createPipeline();
}

void TransparentPass::createLayouts() {
    // Set 0: Global UBO + GBuffer 深度采样器
    RHIBindingLayoutDesc desc;
    desc.entries.push_back({0, RHIDescriptorType::UniformBuffer,
                            RHIShaderStage::Vertex | RHIShaderStage::Fragment, 1});
    desc.entries.push_back({1, RHIDescriptorType::CombinedImageSampler,
                            RHIShaderStage::Fragment, 1});
    globalLayout_ = rhiDevice_->createBindingLayout(desc);

    // 深度采样器（clamp 寻址——屏幕 UV 不应回绕）
    RHISamplerDesc samplerDesc{};
    samplerDesc.minFilter = RHIFilter::Linear;
    samplerDesc.magFilter = RHIFilter::Linear;
    samplerDesc.addressModeU = RHIAddressMode::ClampToEdge;
    samplerDesc.addressModeV = RHIAddressMode::ClampToEdge;
    samplerDesc.addressModeW = RHIAddressMode::ClampToEdge;
    depthSampler_ = rhiDevice_->createSampler(samplerDesc);
}

void TransparentPass::createPipeline() {
    // SrcAlpha/OneMinusSrcAlpha 混合（WaterPass 模板）
    RHIColorBlendAttachment blend{};
    blend.blendEnable = true;
    blend.srcColorFactor = RHIBlendFactor::SrcAlpha;
    blend.dstColorFactor = RHIBlendFactor::OneMinusSrcAlpha;
    blend.colorBlendOp = RHIBlendOp::Add;
    blend.srcAlphaFactor = RHIBlendFactor::One;
    blend.dstAlphaFactor = RHIBlendFactor::Zero;
    blend.alphaBlendOp = RHIBlendOp::Add;

    const auto vertexAttrs = Vertex::getRHIAttributes();

    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    builder->setVertexShader("shaders/pbr_vert.spv")            // 与 ForwardPass 共用顶点着色器
        .setFragmentShader("shaders/transparent_frag.spv")
        .addVertexBinding(0, Vertex::getStride(), RHIVertexInputRate::Vertex);
    for (const auto& a : vertexAttrs) {
        builder->addVertexAttribute(a.binding, a.location, a.format, a.offset);
    }
    builder->setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::None)                          // 透明面片双面
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(false, false, RHICompareOp::Less)          // 深度由 shader 手动剔除
        .setSampleCount(RHISampleCount::Count1)
        .addColorBlendAttachment(blend)
        .addBindingLayout(globalLayout_.get())
        .addBindingLayout(materialLayout_)                       // 复用 ForwardPass 材质布局
        .addPushConstant(RHIShaderStage::Vertex | RHIShaderStage::Fragment,
                         0, sizeof(PushConstantData))
        .setRenderPass(renderPass_);

    pipeline_ = builder->build();
    std::cout << "[TransparentPass] Pipeline created (Pure RHI)" << std::endl;
}

void TransparentPass::createUniformBuffers() {
    uniformBuffers_.resize(maxFramesInFlight_);
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        RHIBufferDesc d{}; d.size = sizeof(UniformBufferObject);
        d.usage = RHIBufferUsage::Uniform; d.memoryUsage = RHIMemoryUsage::CPUToGPU;
        uniformBuffers_[i] = rhiDevice_->createBuffer(d);
    }
}

void TransparentPass::createGlobalBindingGroups() {
    globalBindingGroups_.resize(maxFramesInFlight_);
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        globalBindingGroups_[i] = rhiDevice_->allocateBindingGroup(globalLayout_.get());
        globalBindingGroups_[i]->updateBuffer(0, uniformBuffers_[i].get(), 0, sizeof(UniformBufferObject));
        // binding 1（深度纹理）由 setDepthTexture 填充
    }
}

void TransparentPass::updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo) {
    void* ptr = uniformBuffers_[currentFrame]->map();
    memcpy(ptr, &ubo, sizeof(ubo));
    uniformBuffers_[currentFrame]->unmap();
}

void TransparentPass::setDepthTexture(RHITexture* depthTexture, RHISampler* sampler) {
    depthTexture_ = depthTexture;
    if (!depthTexture_) return;
    RHISampler* s = sampler ? sampler : depthSampler_.get();
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        if (globalBindingGroups_[i]) {
            globalBindingGroups_[i]->updateTexture(1, depthTexture_, s);
        }
    }
}

void TransparentPass::render(RHICommandBuffer* cmd, VEngine::RenderSystem* renderSystem, uint32_t frameIndex) {
    if (!renderSystem || !pipeline_) return;
    auto transparentList = renderSystem->getSortedTransparentList();
    if (transparentList.empty()) return;

    cmd->setViewport(0, 0, float(width_), float(height_));
    cmd->setScissor(0, 0, width_, height_);

    cmd->bindGraphicsPipeline(pipeline_.get());
    cmd->setBindingGroup(0, globalBindingGroups_[frameIndex].get());

    for (const auto* renderable : transparentList) {
        // 复用 ForwardPass 的材质描述符（同一 BindingLayout 对象，兼容）
        if (renderable->materialDescriptor) {
            auto* group = renderable->materialDescriptor->groups[frameIndex].get();
            if (group) cmd->setBindingGroup(1, group);
        }

        PushConstantData pushData{};
        pushData.model = renderable->modelMatrix;
        pushData.normalMatrix = glm::transpose(glm::inverse(renderable->modelMatrix));
        pushData.materialParams = renderable->materialParams;
        cmd->pushConstants(RHIShaderStage::Vertex | RHIShaderStage::Fragment,
                           0, sizeof(PushConstantData), &pushData);

        cmd->bindVertexBuffer(0, renderable->gpuMesh->getVertexBuffer());
        cmd->bindIndexBuffer(renderable->gpuMesh->getIndexBuffer(), 0, RHIIndexType::UInt32);
        cmd->drawIndexed(renderable->gpuMesh->getIndexCount(), 1, 0, 0, 0);
    }
}
