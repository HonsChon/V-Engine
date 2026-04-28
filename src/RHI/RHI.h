#pragma once

// =============================================================================
// RHI — Aggregate Header + Factory
//
// Include this single header to access all RHI abstract interfaces.
// =============================================================================

#include "RHITypes.h"
#include "RHIDevice.h"
#include "RHICommandBuffer.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"
#include "RHIShader.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"
#include "RHISwapChain.h"

#include <memory>

struct GLFWwindow;  // Forward declaration — no GLFW dependency in this header

// =============================================================================
// RHI Factory
// =============================================================================

namespace RHI {

/// Create an RHIDevice for the specified backend.
/// Currently only Vulkan is implemented; DX12 will throw.
std::unique_ptr<RHIDevice> CreateDevice(RHIBackend backend, GLFWwindow* window);

} // namespace RHI
