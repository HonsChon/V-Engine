#pragma once

#include <glm/glm.hpp>
#include <string>
#include <memory>

class VulkanTexture;

struct MaterialProperties {
    glm::vec3 albedo = glm::vec3(0.5f, 0.0f, 0.0f);  // Base color
    float metallic = 0.0f;                             // Metallic factor
    float roughness = 0.5f;                            // Roughness factor
    float ao = 1.0f;                                   // Ambient occlusion
    glm::vec3 emissive = glm::vec3(0.0f);             // Emissive color
};

class Material {
public:
    Material();
    ~Material();

    // Setters for material properties
    void setAlbedo(const glm::vec3& albedo) { properties.albedo = albedo; }
    void setMetallic(float metallic) { properties.metallic = metallic; }
    void setRoughness(float roughness) { properties.roughness = roughness; }
    void setAO(float ao) { properties.ao = ao; }
    void setEmissive(const glm::vec3& emissive) { properties.emissive = emissive; }

    // Texture setters
    void setAlbedoTexture(std::shared_ptr<VulkanTexture> texture);
    void setNormalTexture(std::shared_ptr<VulkanTexture> texture);
    void setMetallicTexture(std::shared_ptr<VulkanTexture> texture);
    void setRoughnessTexture(std::shared_ptr<VulkanTexture> texture);
    void setAOTexture(std::shared_ptr<VulkanTexture> texture);

    // Getters
    const MaterialProperties& getProperties() const { return properties; }
    std::shared_ptr<VulkanTexture> getAlbedoTexture() const { return albedoTexture; }
    std::shared_ptr<VulkanTexture> getNormalTexture() const { return normalTexture; }
    std::shared_ptr<VulkanTexture> getMetallicTexture() const { return metallicTexture; }
    std::shared_ptr<VulkanTexture> getRoughnessTexture() const { return roughnessTexture; }
    std::shared_ptr<VulkanTexture> getAOTexture() const { return aoTexture; }

    // Load material from files
    void loadTextures(const std::string& albedoPath,
                     const std::string& normalPath = "",
                     const std::string& metallicPath = "",
                     const std::string& roughnessPath = "",
                     const std::string& aoPath = "");

    bool hasAlbedoTexture() const { return albedoTexture != nullptr; }
    bool hasNormalTexture() const { return normalTexture != nullptr; }
    bool hasMetallicTexture() const { return metallicTexture != nullptr; }
    bool hasRoughnessTexture() const { return roughnessTexture != nullptr; }
    bool hasAOTexture() const { return aoTexture != nullptr; }

private:
    MaterialProperties properties;
    
    // Texture maps
    std::shared_ptr<VulkanTexture> albedoTexture;
    std::shared_ptr<VulkanTexture> normalTexture;
    std::shared_ptr<VulkanTexture> metallicTexture;
    std::shared_ptr<VulkanTexture> roughnessTexture;
    std::shared_ptr<VulkanTexture> aoTexture;
};