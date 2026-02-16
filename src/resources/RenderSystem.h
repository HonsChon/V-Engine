#pragma once

#include "MeshManager.h"
#include "TextureManager.h"
#include "../scene/Scene.h"
#include "../scene/Components.h"
#include "../passes/RenderPassBase.h"
#include "../passes/ForwardPass.h"
#include "../passes/GBufferPass.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <typeinfo>

namespace VulkanEngine {

/**
 * @brief 可渲染实体数据
 * 缓存实体渲染所需的 GPU 资源引用
 */
struct RenderableEntity {
    entt::entity entityHandle = entt::null;
    std::shared_ptr<GPUMesh> gpuMesh;
    std::shared_ptr<VulkanTexture> albedoTexture;
    std::shared_ptr<VulkanTexture> normalTexture;
    std::shared_ptr<VulkanTexture> specularTexture;
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    bool visible = true;
    bool valid = false;
    
    // ForwardPass 材质描述符引用
    ForwardPass::MaterialDescriptor* materialDescriptor = nullptr;
    
    // GBufferPass 材质描述符引用
    GBufferPass::MaterialDescriptor* gbufferMaterialDescriptor = nullptr;
    
    std::string materialId;  // 用于查找/创建材质描述符
};

/**
 * @brief 渲染系统
 * 负责遍历 ECS 场景并渲染所有可渲染实体
 */
class RenderSystem {
public:
    RenderSystem() = default;
    ~RenderSystem() = default;
    
    /**
     * @brief 初始化渲染系统
     * @param device Vulkan 设备
     */
    void init(std::shared_ptr<VulkanDevice> device) {
        m_device = device;
        
        // 初始化资源管理器
        MeshManager::getInstance().init(device);
        TextureManager::getInstance().init(device);
        
        std::cout << "[RenderSystem] Initialized" << std::endl;
    }
    
    /**
     * @brief 生成材质ID（基于纹理路径）
     */
    std::string generateMaterialId(const std::string& albedo, const std::string& normal, const std::string& metallic) {
        return albedo + "|" + normal + "|" + metallic;
    }
    
    /**
     * @brief 更新渲染数据（使用 RTTI 多态版本）
     * 从场景中收集所有可渲染实体，根据传入的 RenderPass 类型分配相应的材质描述符
     * @param scene 要渲染的场景
     * @param renderPasses 渲染通道列表（支持 ForwardPass、GBufferPass 等）
     */
    void updateRenderables(VulkanEngine::Scene* scene, const std::vector<RenderPassBase*>& renderPasses) {
        if (!scene) return;
        
        auto& registry = scene->getRegistry();
        auto view = registry.view<VulkanEngine::TransformComponent, VulkanEngine::MeshRendererComponent>();
        
        m_renderables.clear();
        
        for (auto entity : view) {
            auto& transform = view.get<VulkanEngine::TransformComponent>(entity);
            auto& meshRenderer = view.get<VulkanEngine::MeshRendererComponent>(entity);
            
            if (!meshRenderer.visible) continue;
            
            RenderableEntity renderable;
            renderable.entityHandle = entity;
            renderable.modelMatrix = transform.getTransform();
            renderable.visible = meshRenderer.visible;
            
            // 获取网格
            renderable.gpuMesh = MeshManager::getInstance().getMesh(meshRenderer.meshPath);
            if (!renderable.gpuMesh || !renderable.gpuMesh->isValid()) {
                continue;  // 跳过无效网格
            }
            
            // 获取纹理和材质ID
            std::string albedoPath, normalPath, metallicPath;
            
            if (registry.all_of<VulkanEngine::PBRMaterialComponent>(entity)) {
                auto& material = registry.get<VulkanEngine::PBRMaterialComponent>(entity);
                
                albedoPath = material.albedoMap;
                normalPath = material.normalMap;
                metallicPath = material.metallicMap;
                
                renderable.albedoTexture = TextureManager::getInstance().getTexture(albedoPath);
                renderable.normalTexture = TextureManager::getInstance().getTexture(normalPath);
                
                if (!metallicPath.empty()) {
                    renderable.specularTexture = TextureManager::getInstance().getTexture(metallicPath);
                } else {
                    renderable.specularTexture = TextureManager::getInstance().getDefaultWhiteTexture();
                    metallicPath = "__default_white__";
                }
            } else {
                // 使用默认纹理
                renderable.albedoTexture = TextureManager::getInstance().getDefaultWhiteTexture();
                renderable.normalTexture = TextureManager::getInstance().getDefaultNormalTexture();
                renderable.specularTexture = TextureManager::getInstance().getDefaultWhiteTexture();
                albedoPath = "__default_white__";
                normalPath = "__default_normal__";
                metallicPath = "__default_white__";
            }
            
            // 生成材质ID
            renderable.materialId = generateMaterialId(albedoPath, normalPath, metallicPath);
            
            // 遍历所有 RenderPass，使用 RTTI 判断类型并分配对应的材质描述符
            for (RenderPassBase* pass : renderPasses) {
                if (!pass) continue;
                
                // 检查纹理是否有效
                bool texturesValid = renderable.albedoTexture && renderable.normalTexture && renderable.specularTexture;
                if (!texturesValid) continue;
                
                // 使用 dynamic_cast 判断 Pass 类型
                if (ForwardPass* forwardPass = dynamic_cast<ForwardPass*>(pass)) {
                    allocateForwardPassDescriptor(renderable, forwardPass);
                }
                else if (GBufferPass* gbufferPass = dynamic_cast<GBufferPass*>(pass)) {
                    allocateGBufferPassDescriptor(renderable, gbufferPass);
                }
                // 可扩展其他 Pass 类型...
            }
            
            renderable.valid = true;
            m_renderables.push_back(renderable);
        }
        
        // 避免每帧输出日志
        static size_t lastCount = 0;
        if (m_renderables.size() != lastCount) {
            std::cout << "[RenderSystem] Updated " << m_renderables.size() << " renderables" << std::endl;
            lastCount = m_renderables.size();
        }
    }
    
    /**
     * @brief 更新渲染数据（旧版兼容接口）
     * @param scene 要渲染的场景
     * @param forwardPass 用于分配材质描述符
     */
    void updateRenderables(VulkanEngine::Scene* scene, ForwardPass* forwardPass) {
        std::vector<RenderPassBase*> passes;
        if (forwardPass) passes.push_back(forwardPass);
        updateRenderables(scene, passes);
    }
    
private:
    /**
     * @brief 为 ForwardPass 分配材质描述符
     */
    void allocateForwardPassDescriptor(RenderableEntity& renderable, ForwardPass* forwardPass) {
        // 尝试获取已有的材质描述符
        renderable.materialDescriptor = forwardPass->getMaterialDescriptor(renderable.materialId);
        
        // 如果不存在，分配新的
        if (!renderable.materialDescriptor) {
            renderable.materialDescriptor = forwardPass->allocateMaterialDescriptor(renderable.materialId);
            
            // 更新纹理绑定
            if (renderable.materialDescriptor) {
                forwardPass->updateMaterialTextures(
                    renderable.materialDescriptor,
                    renderable.albedoTexture->getImageView(),
                    renderable.albedoTexture->getSampler(),
                    renderable.normalTexture->getImageView(),
                    renderable.normalTexture->getSampler(),
                    renderable.specularTexture->getImageView(),
                    renderable.specularTexture->getSampler()
                );
            }
        }
    }
    
    /**
     * @brief 为 GBufferPass 分配材质描述符
     */
    void allocateGBufferPassDescriptor(RenderableEntity& renderable, GBufferPass* gbufferPass) {
        // 尝试获取已有的材质描述符
        renderable.gbufferMaterialDescriptor = gbufferPass->getMaterialDescriptor(renderable.materialId);
        
        // 如果不存在，分配新的
        if (!renderable.gbufferMaterialDescriptor) {
            renderable.gbufferMaterialDescriptor = gbufferPass->allocateMaterialDescriptor(renderable.materialId);
            
            // 更新纹理绑定
            if (renderable.gbufferMaterialDescriptor) {
                gbufferPass->updateMaterialTextures(
                    renderable.gbufferMaterialDescriptor,
                    renderable.albedoTexture->getImageView(),
                    renderable.albedoTexture->getSampler(),
                    renderable.normalTexture->getImageView(),
                    renderable.normalTexture->getSampler(),
                    renderable.specularTexture->getImageView(),
                    renderable.specularTexture->getSampler()
                );
            }
        }
    }
    
public:
    
    /**
     * @brief 统一渲染接口（使用 RTTI 多态）
     * 根据传入的 RenderPass 类型自动调用对应的渲染逻辑
     * @param commandBuffer 命令缓冲
     * @param renderPass RenderPassBase 基类指针（支持 ForwardPass、GBufferPass 等）
     * @param frameIndex 当前帧索引
     */
    void render(VkCommandBuffer commandBuffer, RenderPassBase* renderPass, uint32_t frameIndex) {
        if (!renderPass) return;
        
        // 使用 RTTI 判断 Pass 类型并调用对应的渲染逻辑
        if (ForwardPass* forwardPass = dynamic_cast<ForwardPass*>(renderPass)) {
            renderForwardPass(commandBuffer, forwardPass, frameIndex);
        }
        else if (GBufferPass* gbufferPass = dynamic_cast<GBufferPass*>(renderPass)) {
            renderGBufferPass(commandBuffer, gbufferPass, frameIndex);
        }
        // 可扩展其他 Pass 类型...
    }
    
private:
    /**
     * @brief ForwardPass 渲染实现
     */
    void renderForwardPass(VkCommandBuffer commandBuffer, ForwardPass* forwardPass, uint32_t frameIndex) {
        // 绑定全局描述符集（Set 0: UBO）- 只需绑定一次
        forwardPass->bindGlobalDescriptorSet(commandBuffer, frameIndex);
        
        for (const auto& renderable : m_renderables) {
            if (!renderable.valid || !renderable.gpuMesh) continue;
            
            // 绑定材质描述符集（Set 1: 纹理）- 每个实体独立的描述符
            if (renderable.materialDescriptor) {
                forwardPass->bindMaterialDescriptorSet(commandBuffer, frameIndex, renderable.materialDescriptor);
            }
            
            // 推送模型矩阵（Push Constants）
            forwardPass->pushModelMatrix(commandBuffer, renderable.modelMatrix);
            
            // 绘制网格
            forwardPass->drawMesh(
                commandBuffer,
                renderable.gpuMesh->getVertexBufferHandle(),
                renderable.gpuMesh->getIndexBufferHandle(),
                renderable.gpuMesh->getIndexCount()
            );
        }
    }
    
    /**
     * @brief GBufferPass 渲染实现
     */
    void renderGBufferPass(VkCommandBuffer commandBuffer, GBufferPass* gbufferPass, uint32_t frameIndex) {
        // 绑定全局描述符集（Set 0: UBO）- 只需绑定一次
        gbufferPass->bindGlobalDescriptorSet(commandBuffer, frameIndex);
        
        for (const auto& renderable : m_renderables) {
            if (!renderable.valid || !renderable.gpuMesh) continue;
            
            // 绑定材质描述符集（Set 1: 纹理）- 每个实体独立的描述符
            if (renderable.gbufferMaterialDescriptor) {
                gbufferPass->bindMaterialDescriptorSet(commandBuffer, frameIndex, renderable.gbufferMaterialDescriptor);
            }
            
            // 推送模型矩阵（Push Constants）
            gbufferPass->pushModelMatrix(commandBuffer, renderable.modelMatrix);
            
            // 绘制网格
            gbufferPass->drawMesh(
                commandBuffer,
                renderable.gpuMesh->getVertexBufferHandle(),
                renderable.gpuMesh->getIndexBufferHandle(),
                renderable.gpuMesh->getIndexCount()
            );
        }
    }
    
public:
    
    /**
     * @brief 获取可渲染实体列表（用于射线检测等）
     */
    const std::vector<RenderableEntity>& getRenderables() const {
        return m_renderables;
    }
    
    /**
     * @brief 获取可渲染实体数量
     */
    size_t getRenderableCount() const {
        return m_renderables.size();
    }
    
    /**
     * @brief 获取所有可渲染实体的总顶点数
     */
    uint32_t getTotalVertexCount() const {
        uint32_t total = 0;
        for (const auto& renderable : m_renderables) {
            if (renderable.gpuMesh) {
                total += renderable.gpuMesh->getVertexCount();
            }
        }
        return total;
    }
    
    /**
     * @brief 获取所有可渲染实体的总三角形数
     */
    uint32_t getTotalTriangleCount() const {
        uint32_t total = 0;
        for (const auto& renderable : m_renderables) {
            if (renderable.gpuMesh) {
                total += renderable.gpuMesh->getIndexCount() / 3;
            }
        }
        return total;
    }
    
    /**
     * @brief 获取 Draw Call 数量
     */
    uint32_t getDrawCallCount() const {
        uint32_t count = 0;
        for (const auto& renderable : m_renderables) {
            if (renderable.valid && renderable.gpuMesh) {
                count++;
            }
        }
        return count;
    }
    
    /**
     * @brief 获取 MeshManager（用于查询 AABB 等）
     */
    MeshManager* getMeshManager() {
        return &MeshManager::getInstance();
    }
    
    /**
     * @brief 获取指定实体的 GPUMesh（用于射线检测）
     */
    std::shared_ptr<GPUMesh> getEntityMesh(entt::entity entity) const {
        for (const auto& renderable : m_renderables) {
            if (renderable.entityHandle == entity) {
                return renderable.gpuMesh;
            }
        }
        return nullptr;
    }
    
    /**
     * @brief 清理资源
     */
    void cleanup() {
        m_renderables.clear();
        MeshManager::getInstance().cleanup();
        TextureManager::getInstance().cleanup();
        std::cout << "[RenderSystem] Cleaned up" << std::endl;
    }
    
private:
    std::shared_ptr<VulkanDevice> m_device;
    std::vector<RenderableEntity> m_renderables;
};

} // namespace VulkanEngine