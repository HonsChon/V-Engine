#pragma once

#include "RHITexture.h"
#include "RHISampler.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <iostream>

// Forward declarations
class RHIDevice;

namespace VulkanEngine {

/**
 * @brief GPU Texture 数据结构 (Pure RHI)
 * 持有 RHI 纹理和采样器 — 无 Vulkan 依赖
 */
struct GPUTexture {
    std::unique_ptr<RHITexture> texture;
    std::unique_ptr<RHISampler> sampler;
    uint32_t width = 0;
    uint32_t height = 0;
    
    bool isValid() const { return texture != nullptr && sampler != nullptr; }
    
    RHITexture* getTexture() const { return texture.get(); }
    RHISampler* getSampler() const { return sampler.get(); }
};

/**
 * @brief 纹理资源管理器
 * 负责加载、缓存和管理所有纹理资源
 * 单例模式，全局访问
 * 
 * 通过 RHIDevice 创建纹理和采样器 — 不依赖具体后端
 */
class TextureManager {
public:
    static TextureManager& getInstance() {
        static TextureManager instance;
        return instance;
    }
    
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;
    
    /**
     * @brief 初始化 TextureManager
     * @param rhiDevice RHI 设备指针（用于创建纹理/采样器）
     */
    void init(RHIDevice* rhiDevice) {
        m_rhiDevice = rhiDevice;
        createDefaultTextures();
        std::cout << "[TextureManager] Initialized (RHI)" << std::endl;
    }
    
    /// Set RHI device (called by RenderSystem after init if not provided initially)
    void setRHIDevice(RHIDevice* device) { m_rhiDevice = device; }
    
    /**
     * @brief 加载或获取纹理
     */
    std::shared_ptr<GPUTexture> getTexture(const std::string& texturePath) {
        if (texturePath.empty()) {
            return m_defaultWhiteTexture;
        }
        
        auto it = m_textureCache.find(texturePath);
        if (it != m_textureCache.end()) {
            return it->second;
        }
        
        auto texture = loadTexture(texturePath);
        if (texture) {
            m_textureCache[texturePath] = texture;
            return texture;
        }
        
        return m_defaultWhiteTexture;
    }
    
    std::shared_ptr<GPUTexture> getDefaultWhiteTexture() const { return m_defaultWhiteTexture; }
    std::shared_ptr<GPUTexture> getDefaultNormalTexture() const { return m_defaultNormalTexture; }
    std::shared_ptr<GPUTexture> getDefaultBlackTexture() const { return m_defaultBlackTexture; }
    
    void preloadTexture(const std::string& texturePath) { getTexture(texturePath); }
    
    bool hasTexture(const std::string& texturePath) const {
        return m_textureCache.find(texturePath) != m_textureCache.end();
    }
    
    void unloadTexture(const std::string& texturePath) {
        auto it = m_textureCache.find(texturePath);
        if (it != m_textureCache.end()) {
            std::cout << "[TextureManager] Unloading texture: " << texturePath << std::endl;
            m_textureCache.erase(it);
        }
    }
    
    void cleanup() {
        std::cout << "[TextureManager] Cleaning up " << m_textureCache.size() << " textures..." << std::endl;
        m_textureCache.clear();
        m_defaultWhiteTexture.reset();
        m_defaultNormalTexture.reset();
        m_defaultBlackTexture.reset();
        m_rhiDevice = nullptr;
    }
    
    size_t getTextureCount() const { return m_textureCache.size(); }

private:
    TextureManager() = default;
    ~TextureManager() { cleanup(); }
    
    void createDefaultTextures();
    std::shared_ptr<GPUTexture> loadTexture(const std::string& texturePath);
    std::shared_ptr<GPUTexture> createFromPixels(const unsigned char* pixels,
                                                  uint32_t width, uint32_t height,
                                                  RHIFormat format = RHIFormat::R8G8B8A8_SRGB);
    
    RHIDevice* m_rhiDevice = nullptr;
    std::unordered_map<std::string, std::shared_ptr<GPUTexture>> m_textureCache;
    
    std::shared_ptr<GPUTexture> m_defaultWhiteTexture;
    std::shared_ptr<GPUTexture> m_defaultNormalTexture;
    std::shared_ptr<GPUTexture> m_defaultBlackTexture;
};

} // namespace VulkanEngine