#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>
#include <array>

class VulkanDevice;
class VulkanSwapChain;

class VulkanPipeline {
public:
    VulkanPipeline(std::shared_ptr<VulkanDevice> device, 
                   std::shared_ptr<VulkanSwapChain> swapChain);
    ~VulkanPipeline();

    VkPipeline getPipeline() const;
    VkPipelineLayout getPipelineLayout() const;
    VkDescriptorSetLayout getDescriptorSetLayout() const;

private:
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void cleanup();
    
    std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);

    std::shared_ptr<VulkanDevice> device;
    std::shared_ptr<VulkanSwapChain> swapChain;

    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
};