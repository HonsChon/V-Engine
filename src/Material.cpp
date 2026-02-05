// 快速占位符实现
#include "Material.h"
#include "VulkanTexture.h"
#include <iostream>
Material::Material() { std::cout << "Material placeholder" << std::endl; }
Material::~Material() {}
void Material::setAlbedoTexture(std::shared_ptr<VulkanTexture> texture) { albedoTexture = texture; }
void Material::setNormalTexture(std::shared_ptr<VulkanTexture> texture) { normalTexture = texture; }
void Material::setMetallicTexture(std::shared_ptr<VulkanTexture> texture) { metallicTexture = texture; }
void Material::setRoughnessTexture(std::shared_ptr<VulkanTexture> texture) { roughnessTexture = texture; }
void Material::setAOTexture(std::shared_ptr<VulkanTexture> texture) { aoTexture = texture; }
void Material::loadTextures(const std::string& albedoPath, const std::string& normalPath, const std::string& metallicPath, const std::string& roughnessPath, const std::string& aoPath) {}