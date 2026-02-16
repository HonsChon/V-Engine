#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>

class VulkanDevice;

class VulkanTexture {
public:
    // 默认构造函数（需要后续调用 loadFromFile 或 createDefault*）
    VulkanTexture(std::shared_ptr<VulkanDevice> device);
    
    // 从文件加载纹理的构造函数
    VulkanTexture(std::shared_ptr<VulkanDevice> device, const std::string& filepath);
    
    ~VulkanTexture();

    // 禁止拷贝
    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;

    // 从文件加载纹理
    bool loadFromFile(const std::string& filepath);
    
    // 创建默认白色纹理（1x1）
    void createDefaultTexture(uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t a = 255);
    
    // 创建默认法线纹理（1x1，指向正Z）
    void createDefaultNormalTexture();

    VkImageView getImageView() const { return imageView; }
    VkSampler getSampler() const { return sampler; }
    VkImage getImage() const { return image; }
    
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }
    
    // 静态工厂方法：创建默认纹理
    static std::unique_ptr<VulkanTexture> createDefaultTextureStatic(
        std::shared_ptr<VulkanDevice> device, 
        uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t a = 255);

private:
    void createTextureImage(const std::string& filepath);
    // format 参数：默认 SRGB 用于颜色贴图，法线贴图应使用 UNORM
    void createTextureImageFromMemory(const unsigned char* pixels, int width, int height, int channels,
                                      VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
    void createImage(uint32_t width, uint32_t height, VkFormat format, 
                    VkImageTiling tiling, VkImageUsageFlags usage,
                    VkMemoryPropertyFlags properties, VkImage& image, 
                    VkDeviceMemory& imageMemory);
    void transitionImageLayout(VkImage image, VkFormat format, 
                              VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    void createTextureImageView(VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
    void createTextureSampler();
    
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    std::shared_ptr<VulkanDevice> device;
    
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    
    uint32_t width = 0;
    uint32_t height = 0;
};