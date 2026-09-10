#include "VulkanRHIShader.h"
#include "VulkanRHIDevice.h"
#include <stdexcept>
#include <fstream>

VulkanRHIShader::VulkanRHIShader(VulkanRHIDevice* device, RHIShaderStage stage,
                                 const std::vector<uint32_t>& spirvCode,
                                 const std::string& entryPoint)
    : device_(device), stage_(stage), entryPoint_(entryPoint)
{
    createShaderModule(spirvCode);
}

VulkanRHIShader::VulkanRHIShader(VulkanRHIDevice* device, RHIShaderStage stage,
                                 const std::string& filePath,
                                 const std::string& entryPoint)
    : device_(device), stage_(stage), entryPoint_(entryPoint)
{
    auto spirvCode = readSPIRVFile(filePath);
    createShaderModule(spirvCode);
}

VulkanRHIShader::~VulkanRHIShader() {
    if (shaderModule_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_->getVkDevice(), shaderModule_, nullptr);
    }
}

void VulkanRHIShader::createShaderModule(const std::vector<uint32_t>& spirvCode) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
    createInfo.pCode = spirvCode.data();

    if (vkCreateShaderModule(device_->getVkDevice(), &createInfo, nullptr, &shaderModule_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIShader] Failed to create shader module!");
    }
}

std::vector<uint32_t> VulkanRHIShader::readSPIRVFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("[VulkanRHIShader] Failed to open shader file: " + path);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    if (fileSize % sizeof(uint32_t) != 0) {
        throw std::runtime_error("[VulkanRHIShader] SPIR-V file size is not aligned: " + path);
    }

    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    return buffer;
}
