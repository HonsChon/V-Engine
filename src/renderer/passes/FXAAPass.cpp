/**
 * @file FXAAPass.cpp
 * @brief FXAA 后处理实现（全屏三角形，SSRPass 同款管线模式）
 */

#include "FXAAPass.h"

#include "RHIDevice.h"
#include "RHIPipeline.h"
#include "RHIDescriptor.h"
#include "RHIRenderPass.h"
#include "RHICommandBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"

#include <iostream>

FXAAPass::FXAAPass(RHIDevice* rhiDevice, RHIRenderPass* renderPass,
                   uint32_t width, uint32_t height)
    : RenderPassBase(rhiDevice, width, height)
    , rhiDevice_(rhiDevice)
    , renderPass_(renderPass)
    , width_(width)
    , height_(height)
{
    passName = "FXAA Pass";

    // Set 0: 场景颜色纹理
    RHIBindingLayoutDesc layoutDesc;
    layoutDesc.entries.push_back({0, RHIDescriptorType::CombinedImageSampler,
                                  RHIShaderStage::Fragment, 1});
    bindingLayout_ = rhiDevice_->createBindingLayout(layoutDesc);
    bindingGroup_ = rhiDevice_->allocateBindingGroup(bindingLayout_.get());

    // 后处理采样器：Clamp 寻址（屏幕 UV 不回绕）
    RHISamplerDesc samplerDesc{};
    samplerDesc.magFilter = RHIFilter::Linear;
    samplerDesc.minFilter = RHIFilter::Linear;
    samplerDesc.addressModeU = RHIAddressMode::ClampToEdge;
    samplerDesc.addressModeV = RHIAddressMode::ClampToEdge;
    samplerDesc.addressModeW = RHIAddressMode::ClampToEdge;
    sampler_ = rhiDevice_->createSampler(samplerDesc);

    createPipeline();
    std::cout << "[FXAAPass] Created (Pure RHI): " << width_ << "x" << height_ << std::endl;
}

FXAAPass::~FXAAPass() { cleanup(); }

void FXAAPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();
    bindingGroup_.reset();
    pipeline_.reset();
    bindingLayout_.reset();
    sampler_.reset();
}

void FXAAPass::recreate(RHIRenderPass* newRenderPass, uint32_t newWidth, uint32_t newHeight) {
    if (rhiDevice_) rhiDevice_->waitIdle();
    pipeline_.reset();
    renderPass_ = newRenderPass;
    width_ = newWidth;
    height_ = newHeight;
    createPipeline();
}

void FXAAPass::createPipeline() {
    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    builder->setVertexShader("shaders/fxaa_vert.spv")
        .setFragmentShader("shaders/fxaa_frag.spv")
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::None)
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(false, false, RHICompareOp::Less)
        .setSampleCount(RHISampleCount::Count1)
        .setColorAttachmentCount(1)
        .addBindingLayout(bindingLayout_.get())
        .addPushConstant(RHIShaderStage::Fragment, 0, 16)
        .setRenderPass(renderPass_);

    pipeline_ = builder->build();
    std::cout << "[FXAAPass] Pipeline created (Pure RHI)" << std::endl;
}

void FXAAPass::setSourceTexture(RHITexture* texture, RHISampler* sampler) {
    if (!texture || !bindingGroup_) return;
    bindingGroup_->updateTexture(0, texture, sampler ? sampler : sampler_.get());
}

void FXAAPass::render(RHICommandBuffer* cmd, uint32_t frameIndex, bool enabled) {
    if (!pipeline_ || !bindingGroup_) return;
    (void)frameIndex;

    cmd->setViewport(0, 0, float(width_), float(height_));
    cmd->setScissor(0, 0, width_, height_);

    cmd->bindGraphicsPipeline(pipeline_.get());
    cmd->setBindingGroup(0, bindingGroup_.get());

    // push constant: 1/尺寸 + 开关
    glm::vec4 params(1.0f / float(width_), 1.0f / float(height_),
                     enabled ? 1.0f : 0.0f, 0.0f);
    cmd->pushConstants(RHIShaderStage::Fragment, 0, sizeof(params), &params);

    cmd->draw(3, 1, 0, 0);   // 全屏三角形
}
