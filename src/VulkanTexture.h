#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>

class VulkanDevice;

class VulkanTexture {
public:
    VulkanTexture(std::shared_ptr<VulkanDevice> device);
    ~VulkanTexture();

    void loadFromFile(const std::string& path);
    void createFromData(const void* data, int width, int height, int channels);
    void createDefault(int width = 1, int height = 1);

    VkImage getImage() const { return image; }
    VkImageView getImageView() const { return imageView; }
    VkSampler getSampler() const { return sampler; }

private:
    void createImage(int width, int height, VkFormat format);
    void createImageView(VkFormat format);
    void createSampler();
    void transitionImageLayout(VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, uint32_t width, uint32_t height);

    std::shared_ptr<VulkanDevice> device;
    
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};