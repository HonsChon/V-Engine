#pragma once

#include "RHITypes.h"
#include <memory>
#include <string>

// =============================================================================
// RHI Shader — Abstract Interface
// =============================================================================

class RHIShader {
public:
    virtual ~RHIShader() = default;

    virtual RHIShaderStage getStage() const = 0;

    // Non-copyable
    RHIShader(const RHIShader&) = delete;
    RHIShader& operator=(const RHIShader&) = delete;

protected:
    RHIShader() = default;
};
