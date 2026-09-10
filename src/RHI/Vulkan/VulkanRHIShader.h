#pragma once

#include "RHIShader.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

class VulkanRHIDevice;

class VulkanRHIShader : public RHIShader {
public:
    /// Create from pre-loaded SPIR-V binary data
    VulkanRHIShader(VulkanRHIDevice* device, RHIShaderStage stage,
                    const std::vector<uint32_t>& spirvCode,
                    const std::string& entryPoint = "main");

    /// Create from SPIR-V file path
    VulkanRHIShader(VulkanRHIDevice* device, RHIShaderStage stage,
                    const std::string& filePath,
                    const std::string& entryPoint = "main");

    ~VulkanRHIShader() override;

    RHIShaderStage getStage() const override { return stage_; }
    const std::string& getEntryPoint() const { return entryPoint_; }

    VkShaderModule getVkShaderModule() const { return shaderModule_; }

private:
    void createShaderModule(const std::vector<uint32_t>& spirvCode);
    static std::vector<uint32_t> readSPIRVFile(const std::string& path);

    VulkanRHIDevice* device_;
    RHIShaderStage   stage_;
    std::string      entryPoint_;
    VkShaderModule   shaderModule_ = VK_NULL_HANDLE;
};
