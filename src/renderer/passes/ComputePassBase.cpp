#include "ComputePassBase.h"
#include "RHIDevice.h"
#include "RHIPipeline.h"
#include "RHIDescriptor.h"
#include "RHICommandBuffer.h"
#include "RHIBuffer.h"
#include <iostream>

ComputePassBase::ComputePassBase(RHIDevice* rhiDevice, const std::string& name)
    : rhiDevice_(rhiDevice), name(name) {
}

ComputePassBase::~ComputePassBase() {
    cleanup();
}

void ComputePassBase::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();
    pipeline_.reset();
    bindingLayout_.reset();
}

void ComputePassBase::insertBufferBarrier(RHICommandBuffer* cmd, RHIBuffer* buffer) {
    cmd->bufferBarrier(buffer, buffer->getSize(),
                       RHIPipelineStage::ComputeShader, RHIPipelineStage::ComputeShader,
                       RHIAccessFlags::ShaderWrite, RHIAccessFlags::ShaderRead);
}

