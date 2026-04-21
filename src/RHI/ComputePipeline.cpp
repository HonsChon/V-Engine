#include "ComputePipeline.h"
#include "VulkanDevice.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <iostream>

ComputePipeline::ComputePipeline(std::shared_ptr<VulkanDevice> device, const Config& config)
    : device(device), storedBindings(config.bindings) {
    
    createDescriptorSetLayout(config.bindings);
    createPipelineLayout(config.pushConstantRanges);
    createPipeline(config.shaderPath, config.entryPoint);
    createDescriptorPool();
    
    std::cout << "[ComputePipeline] Created compute pipeline: " << config.shaderPath << std::endl;
}

ComputePipeline::~ComputePipeline() {
    VkDevice vkDevice = device->getDevice();
    
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
    }
    
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkDevice, pipeline, nullptr);
    }
    
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
    }
    
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
    }
}

void ComputePipeline::bind(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
}

void ComputePipeline::dispatch(VkCommandBuffer commandBuffer, 
                               uint32_t groupCountX, 
                               uint32_t groupCountY, 
                               uint32_t groupCountZ) {
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void ComputePipeline::dispatchIndirect(VkCommandBuffer commandBuffer, 
                                       VkBuffer buffer, 
                                       VkDeviceSize offset) {
    vkCmdDispatchIndirect(commandBuffer, buffer, offset);
}

VkDescriptorSet ComputePipeline::createDescriptorSet(VkDescriptorPool pool) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate compute descriptor set!");
    }

    return descriptorSet;
}

void ComputePipeline::createDescriptorSetLayout(const std::vector<DescriptorBinding>& bindings) {
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    layoutBindings.reserve(bindings.size());

    for (const auto& binding : bindings) {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding.binding;
        layoutBinding.descriptorType = binding.type;
        layoutBinding.descriptorCount = binding.count;
        layoutBinding.stageFlags = binding.stageFlags;
        layoutBinding.pImmutableSamplers = nullptr;
        
        layoutBindings.push_back(layoutBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute descriptor set layout!");
    }
}

void ComputePipeline::createPipelineLayout(const std::vector<PushConstantRange>& pushConstantRanges) {
    std::vector<VkPushConstantRange> vkRanges;
    vkRanges.reserve(pushConstantRanges.size());

    for (const auto& range : pushConstantRanges) {
        VkPushConstantRange vkRange{};
        vkRange.stageFlags = range.stageFlags;
        vkRange.offset = range.offset;
        vkRange.size = range.size;
        vkRanges.push_back(vkRange);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(vkRanges.size());
    pipelineLayoutInfo.pPushConstantRanges = vkRanges.empty() ? nullptr : vkRanges.data();

    if (vkCreatePipelineLayout(device->getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline layout!");
    }
}

void ComputePipeline::createPipeline(const std::string& shaderPath, const std::string& entryPoint) {
    // 读取着色器代码
    auto shaderCode = readShaderFile(shaderPath);
    VkShaderModule shaderModule = createShaderModule(shaderCode);

    // 着色器阶段
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = entryPoint.c_str();

    // 创建计算管线
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateComputePipelines(device->getDevice(), VK_NULL_HANDLE, 1, 
                                  &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline!");
    }

    // 销毁着色器模块（管线创建后不再需要）
    vkDestroyShaderModule(device->getDevice(), shaderModule, nullptr);
}

std::vector<char> ComputePipeline::readShaderFile(const std::string& filename) {
    // 先尝试获取绝对路径用于调试
    std::filesystem::path absPath = std::filesystem::absolute(filename);
    std::cout << "[ComputePipeline] Loading shader: " << filename 
              << " -> " << absPath.string() << std::endl;
    
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename + 
                                 " (abs: " + absPath.string() + ")");
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    
    std::cout << "[ComputePipeline] Shader file size: " << fileSize << " bytes" << std::endl;

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

VkShaderModule ComputePipeline::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shaderModule;
}

void ComputePipeline::createDescriptorPool() {
    // 根据 bindings 创建 pool sizes
    std::unordered_map<VkDescriptorType, uint32_t> typeCounts;
    
    for (const auto& binding : storedBindings) {
        typeCounts[binding.type] += binding.count;
    }
    
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (const auto& [type, count] : typeCounts) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = type;
        poolSize.descriptorCount = count * 16;  // 支持多个描述符集
        poolSizes.push_back(poolSize);
    }
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 16;  // 最�?16 个描述符�?
    
    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute descriptor pool!");
    }
}

VkDescriptorSet ComputePipeline::allocateDescriptorSet() {
    if (descriptorPool == VK_NULL_HANDLE) {
        createDescriptorPool();
    }
    
    return createDescriptorSet(descriptorPool);
}
