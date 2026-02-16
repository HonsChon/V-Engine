#pragma once

#include "VulkanTexture.h"
#include "VulkanDevice.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <iostream>

namespace VulkanEngine {

/**
 * @brief 纹理资源管理器
 * 负责加载、缓存和管理所有纹理资源
 * 单例模式，全局访问
 */
class TextureManager {
public:
    static TextureManager& getInstance() {
        static TextureManager instance;
        return instance;
    }
    
    // 禁止拷贝和移动
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;
    
    /**
     * @brief 初始化 TextureManager
     * @param device Vulkan 设备指针
     */
    void init(std::shared_ptr<VulkanDevice> device) {
        m_device = device;
        createDefaultTextures();
        std::cout << "[TextureManager] Initialized" << std::endl;
    }
    
    /**
     * @brief 加载或获取纹理
     * 如果纹理已缓存，直接返回；否则加载并缓存
     * @param texturePath 纹理文件路径
     * @return 指向 VulkanTexture 的共享指针，失败返回默认白色纹理
     */
    std::shared_ptr<VulkanTexture> getTexture(const std::string& texturePath) {
        if (texturePath.empty()) {
            return m_defaultWhiteTexture;
        }
        
        // 检查缓存
        auto it = m_textureCache.find(texturePath);
        if (it != m_textureCache.end()) {
            return it->second;
        }
        
        // 加载纹理
        auto texture = loadTexture(texturePath);
        if (texture) {
            m_textureCache[texturePath] = texture;
            return texture;
        }
        
        // 加载失败返回默认纹理
        return m_defaultWhiteTexture;
    }
    
    /**
     * @brief 获取默认白色纹理
     */
    std::shared_ptr<VulkanTexture> getDefaultWhiteTexture() const {
        return m_defaultWhiteTexture;
    }
    
    /**
     * @brief 获取默认法线纹理（指向 +Z 的蓝色）
     */
    std::shared_ptr<VulkanTexture> getDefaultNormalTexture() const {
        return m_defaultNormalTexture;
    }
    
    /**
     * @brief 获取默认黑色纹理
     */
    std::shared_ptr<VulkanTexture> getDefaultBlackTexture() const {
        return m_defaultBlackTexture;
    }
    
    /**
     * @brief 预加载纹理（不返回，仅缓存）
     */
    void preloadTexture(const std::string& texturePath) {
        getTexture(texturePath);
    }
    
    /**
     * @brief 检查纹理是否已缓存
     */
    bool hasTexture(const std::string& texturePath) const {
        return m_textureCache.find(texturePath) != m_textureCache.end();
    }
    
    /**
     * @brief 卸载指定纹理
     */
    void unloadTexture(const std::string& texturePath) {
        auto it = m_textureCache.find(texturePath);
        if (it != m_textureCache.end()) {
            std::cout << "[TextureManager] Unloading texture: " << texturePath << std::endl;
            m_textureCache.erase(it);
        }
    }
    
    /**
     * @brief 卸载所有纹理资源
     */
    void cleanup() {
        std::cout << "[TextureManager] Cleaning up " << m_textureCache.size() << " textures..." << std::endl;
        m_textureCache.clear();
        m_defaultWhiteTexture.reset();
        m_defaultNormalTexture.reset();
        m_defaultBlackTexture.reset();
        m_device.reset();
    }
    
    /**
     * @brief 获取已加载的纹理数量
     */
    size_t getTextureCount() const {
        return m_textureCache.size();
    }

private:
    TextureManager() = default;
    ~TextureManager() { cleanup(); }
    
    /**
     * @brief 创建默认纹理
     */
    void createDefaultTextures() {
        if (!m_device) return;
        
        // 默认白色纹理
        m_defaultWhiteTexture = std::make_shared<VulkanTexture>(m_device);
        m_defaultWhiteTexture->createDefaultTexture();
        
        // 默认法线纹理
        m_defaultNormalTexture = std::make_shared<VulkanTexture>(m_device);
        m_defaultNormalTexture->createDefaultNormalTexture();
        
        // 默认黑色纹理
        m_defaultBlackTexture = std::make_shared<VulkanTexture>(m_device);
        m_defaultBlackTexture->createDefaultTexture(0, 0, 0, 255);
        
        std::cout << "[TextureManager] Default textures created" << std::endl;
    }
    
    /**
     * @brief 实际加载纹理的内部方法
     */
    std::shared_ptr<VulkanTexture> loadTexture(const std::string& texturePath) {
        if (!m_device) {
            std::cerr << "[TextureManager] Error: Device not initialized!" << std::endl;
            return nullptr;
        }
        
        auto texture = std::make_shared<VulkanTexture>(m_device);
        if (texture->loadFromFile(texturePath)) {
            std::cout << "[TextureManager] Loaded texture: " << texturePath << std::endl;
            return texture;
        }
        
        std::cerr << "[TextureManager] Failed to load texture: " << texturePath << std::endl;
        return nullptr;
    }
    
    std::shared_ptr<VulkanDevice> m_device;
    std::unordered_map<std::string, std::shared_ptr<VulkanTexture>> m_textureCache;
    
    // 默认纹理
    std::shared_ptr<VulkanTexture> m_defaultWhiteTexture;
    std::shared_ptr<VulkanTexture> m_defaultNormalTexture;
    std::shared_ptr<VulkanTexture> m_defaultBlackTexture;
};

} // namespace VulkanEngine
