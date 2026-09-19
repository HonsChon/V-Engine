/**
 * @file ReflectionPass.cpp
 * @brief 水面平面反射 — 镜像相机渲染场景到半分辨率 RT (Pure RHI)
 */

#include "ReflectionPass.h"

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

ReflectionPass::ReflectionPass(RHIDevice* rhiDevice,
                               uint32_t width, uint32_t height,
                               RHIBindingLayout* materialLayout,
                               uint32_t maxFramesInFlight)
    : RenderPassBase(rhiDevice, width, height)
    , rhiDevice_(rhiDevice)
    , materialLayout_(materialLayout)
    , width_(width)
    , height_(height)
    , maxFramesInFlight_(maxFramesInFlight)
{
    passName = "Reflection Pass";

    // 半分辨率（向上取整防 0）
    rtWidth_  = (width_ + 1) / 2;
    rtHeight_ = (height_ + 1) / 2;

    createRenderTargets();
    createRenderPassAndFramebuffer();
    createSampler();
    createBindingLayout();
    createUniformBuffers();
    createBindingGroups();
    createPipeline();

    std::cout << "ReflectionPass created (Pure RHI): RT " << rtWidth_ << "x" << rtHeight_
              << " (half of " << width_ << "x" << height_ << ")" << std::endl;
}

ReflectionPass::~ReflectionPass() { cleanup(); }

void ReflectionPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();
    bindingGroups_.clear();
    uniformBuffers_.clear();
    pipeline_.reset();
    globalLayout_.reset();
    framebuffer_.reset();
    renderPass_.reset();
    sampler_.reset();
    depthTexture_.reset();
    colorTexture_.reset();
}

void ReflectionPass::resize(uint32_t newWidth, uint32_t newHeight) {
    uint32_t newRtWidth  = (newWidth + 1) / 2;
    uint32_t newRtHeight = (newHeight + 1) / 2;
    if (newRtWidth == rtWidth_ && newRtHeight == rtHeight_) {
        width_ = newWidth; height_ = newHeight;
        return;
    }
    if (rhiDevice_) rhiDevice_->waitIdle();

    framebuffer_.reset();
    depthTexture_.reset();
    colorTexture_.reset();

    width_ = newWidth; height_ = newHeight;
    rtWidth_ = newRtWidth; rtHeight_ = newRtHeight;

    createRenderTargets();
    createRenderPassAndFramebuffer();   // FB 引用新纹理，重建（RP 格式不变）

    std::cout << "ReflectionPass resized (Pure RHI): RT " << rtWidth_ << "x" << rtHeight_ << std::endl;
}

// =============================================================================
// RHI Resource Creation
// =============================================================================

void ReflectionPass::createRenderTargets() {
    // 颜色 RT — 与主画面同格式（pbr.frag 输出已 tonemap+gamma 的 LDR）
    {
        RHITextureDesc desc{};
        desc.width = rtWidth_;
        desc.height = rtHeight_;
        desc.format = RHIFormat::R8G8B8A8_UNORM;
        desc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled;
        colorTexture_ = rhiDevice_->createTexture(desc);
    }
    // 深度 — 仅反射渲染期使用，无需采样
    {
        RHITextureDesc desc{};
        desc.width = rtWidth_;
        desc.height = rtHeight_;
        desc.format = RHIFormat::D32_SFLOAT;
        desc.usage = RHITextureUsage::DepthStencilAttachment;
        depthTexture_ = rhiDevice_->createTexture(desc);
    }
}

void ReflectionPass::createRenderPassAndFramebuffer() {
    RHIRenderPassDesc rpDesc{};
    // finalLayout=ShaderReadOnly：结束后即处于可采样布局（双后端一致）
    rpDesc.addColorAttachment(
        RHIFormat::R8G8B8A8_UNORM,
        RHILoadOp::Clear, RHIStoreOp::Store,
        RHIImageLayout::Undefined, RHIImageLayout::ShaderReadOnly);
    rpDesc.setDepthAttachment(
        RHIFormat::D32_SFLOAT,
        RHILoadOp::Clear, RHIStoreOp::Store,
        RHIImageLayout::Undefined, RHIImageLayout::DepthStencilAttachment);
    renderPass_ = rhiDevice_->createRenderPass(rpDesc);

    RHIFramebufferDesc fbDesc{};
    fbDesc.renderPass = renderPass_.get();
    fbDesc.width = rtWidth_;
    fbDesc.height = rtHeight_;
    fbDesc.attachments.push_back(colorTexture_.get());
    fbDesc.attachments.push_back(depthTexture_.get());
    framebuffer_ = rhiDevice_->createFramebuffer(fbDesc);
}

void ReflectionPass::createSampler() {
    // Linear + Clamp：屏幕 UV 不应回绕
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

void ReflectionPass::createBindingLayout() {
    // Set 0: Global UBO（与 pbr 着色器 Set 0 一致）
    RHIBindingLayoutDesc desc;
    desc.entries.push_back({0, RHIDescriptorType::UniformBuffer,
                            RHIShaderStage::Vertex | RHIShaderStage::Fragment, 1});
    globalLayout_ = rhiDevice_->createBindingLayout(desc);
    // Set 1: 材质（materialLayout_，ForwardPass 拥有）
}

void ReflectionPass::createUniformBuffers() {
    uniformBuffers_.resize(maxFramesInFlight_);
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        RHIBufferDesc desc{};
        desc.size = sizeof(UniformBufferObject);
        desc.usage = RHIBufferUsage::Uniform;
        desc.memoryUsage = RHIMemoryUsage::CPUToGPU;
        uniformBuffers_[i] = rhiDevice_->createBuffer(desc);
    }
}

void ReflectionPass::createBindingGroups() {
    bindingGroups_.resize(maxFramesInFlight_);
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        bindingGroups_[i] = rhiDevice_->allocateBindingGroup(globalLayout_.get());
        bindingGroups_[i]->updateBuffer(0, uniformBuffers_[i].get(), 0, sizeof(UniformBufferObject));
    }
}

void ReflectionPass::createPipeline() {
    const auto vertexAttrs = Vertex::getRHIAttributes();

    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    builder->setVertexShader("shaders/pbr_vert.spv")       // 复用 PBR 着色器（相机信息全部来自 UBO）
        .setFragmentShader("shaders/pbr_frag.spv")
        .addVertexBinding(0, Vertex::getStride(), RHIVertexInputRate::Vertex);
    for (const auto& a : vertexAttrs) {
        builder->addVertexAttribute(a.binding, a.location, a.format, a.offset);
    }
    builder->setTopology(RHIPrimitiveTopology::TriangleList)
        // 绕序：镜像 view（det=-1）翻转屏幕绕序 → 外表面（贴水侧）在反射渲染中
        // 的绕序与主画面相反。主画面用 CCW 渲染外表面，故反射管线需 Clockwise
        // 才能保留外表面；若用 CCW 会剔除外表面、渲染出内表面（倒影呈"球内部"，
        // pbr.frag 的 gl_FrontFacing 法线翻转会掩盖错误使其看似合理）。
        // 注：此前曾因"RT 只剩远水一半"误诊为壳反、改过 CCW——真正根因是
        // 深度约定错配（见 makeObliqueProjection 注释），与绕序无关。
        .setCullMode(RHICullMode::Back)
        .setFrontFace(RHIFrontFace::Clockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(true, true, RHICompareOp::Less)      // 反射渲染正常深度测试
        .setSampleCount(RHISampleCount::Count1)
        .setColorAttachmentCount(1)                        // 不透明，无 blend
        .addBindingLayout(globalLayout_.get())
        .addBindingLayout(materialLayout_)                 // 复用 ForwardPass 材质布局
        .addPushConstant(RHIShaderStage::Vertex | RHIShaderStage::Fragment,
                         0, sizeof(PushConstantData))
        .setRenderPass(renderPass_.get());

    pipeline_ = builder->build();
    std::cout << "ReflectionPass pipeline created (Pure RHI, reuses pbr shaders)" << std::endl;
}

// =============================================================================
// Updates & Render
// =============================================================================

void ReflectionPass::updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo) {
    void* ptr = uniformBuffers_[currentFrame]->map();
    memcpy(ptr, &ubo, sizeof(ubo));
    uniformBuffers_[currentFrame]->unmap();
}

void ReflectionPass::render(RHICommandBuffer* cmd, VEngine::RenderSystem* renderSystem,
                            uint32_t frameIndex, bool renderGeometry) {
    if (!cmd || !pipeline_) return;

    // 清屏色与主画面一致（未命中区域回退天空色）
    std::vector<RHIClearValue> clearValues = {
        RHIClearValue::Color(0.1f, 0.2f, 0.4f, 1.0f),
        RHIClearValue::DepthStencil(1.0f, 0)
    };
    cmd->beginRenderPass(renderPass_.get(), framebuffer_.get(), clearValues);
    cmd->setViewport(0, 0, (float)rtWidth_, (float)rtHeight_);
    cmd->setScissor(0, 0, rtWidth_, rtHeight_);

    if (renderGeometry && renderSystem) {
        cmd->bindGraphicsPipeline(pipeline_.get());
        cmd->setBindingGroup(0, bindingGroups_[frameIndex].get());

        // 不透明 + Mask 几何（透明物体不进反射；RenderSystem 已每帧分配
        // ForwardPass 材质描述符，Set 1 布局相同直接兼容）
        for (const auto& renderable : renderSystem->getRenderables()) {
            if (!renderable.valid || !renderable.gpuMesh) continue;
            if (renderable.transparent) continue;
            if (!renderable.materialDescriptor || !renderable.materialDescriptor->valid) continue;

            auto* group = renderable.materialDescriptor->groups[frameIndex].get();
            if (group) cmd->setBindingGroup(1, group);

            PushConstantData pushData{};
            pushData.model = renderable.modelMatrix;
            pushData.normalMatrix = glm::transpose(glm::inverse(renderable.modelMatrix));
            pushData.materialParams = renderable.materialParams;
            cmd->pushConstants(RHIShaderStage::Vertex | RHIShaderStage::Fragment,
                               0, sizeof(PushConstantData), &pushData);

            cmd->bindVertexBuffer(0, renderable.gpuMesh->getVertexBuffer());
            cmd->bindIndexBuffer(renderable.gpuMesh->getIndexBuffer(), 0, RHIIndexType::UInt32);
            cmd->drawIndexed(renderable.gpuMesh->getIndexCount(), 1, 0, 0, 0);
        }
    }

    cmd->endRenderPass();
}
