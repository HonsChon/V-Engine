#pragma once

#include "MeshManager.h"
#include "TextureManager.h"
#include "Scene.h"
#include "Components.h"
#include "RenderPassBase.h"
#include "ForwardPass.h"
#include "GBufferPass.h"
#include "RHICommandBuffer.h"
#include "RHIBuffer.h"
#include "RHIDevice.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <typeinfo>

namespace VEngine {

/**
 * @brief 可渲染实体数据
 * 缓存实体渲染所需的GPU 资源引用
 */
struct RenderableEntity {
    entt::entity entityHandle = entt::null;
    std::shared_ptr<GPUMesh> gpuMesh;
    std::shared_ptr<GPUTexture> albedoTexture;
    std::shared_ptr<GPUTexture> normalTexture;
    std::shared_ptr<GPUTexture> specularTexture;
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    bool visible = true;
    bool valid = false;

    // 透明渲染支持
    bool transparent = false;                          // Blend 模式 → 透明队列
    float cameraDistance = 0.0f;                        // 用于 back-to-front 排序
    glm::vec4 materialParams = glm::vec4(1.0f, 0.0f, 0.5f, 0.0f);  // x=opacity y=alphaMode z=cutoff

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
     * @param rhiDevice RHI 设备指针
     */
    void init(RHIDevice* rhiDevice) {
        m_rhiDevice = rhiDevice;
        
        // 初始化资源管理器 (Pure RHI)
        MeshManager::getInstance().init(rhiDevice);
        TextureManager::getInstance().init(rhiDevice);
        
        std::cout << "[RenderSystem] Initialized" << std::endl;
    }
    
    /**
     * @brief 生成材质ID（基于纹理路径）
     */
    std::string generateMaterialId(const std::string& albedo, const std::string& normal, const std::string& metallic) {
        return albedo + "|" + normal + "|" + metallic;
    }
    
    /**
     * @brief 更新渲染数据（使用RTTI 多态版本）
     * 从场景中收集所有可渲染实体，根据传入的 RenderPass 的类型分配相应的材质描述符
     * @param scene 要渲染的场景
     * @param renderPasses 渲染通道列表（支持ForwardPass、GBufferPass 等）
     * @param cameraPos 相机世界位置（用于透明物体 back-to-front 排序，可为空）
     */
    void updateRenderables(VEngine::Scene* scene, const std::vector<RenderPassBase*>& renderPasses,
                           const glm::vec3& cameraPos = glm::vec3(0.0f)) {
        if (!scene) return;

        auto& registry = scene->getRegistry();
        auto view = registry.view<VEngine::TransformComponent, VEngine::MeshRendererComponent>();

        m_renderables.clear();
        
        for (auto entity : view) {
            auto& meshRenderer = view.get<VEngine::MeshRendererComponent>(entity);
            
            if (!meshRenderer.visible) continue;
            
            RenderableEntity renderable;
            renderable.entityHandle = entity;
            renderable.modelMatrix = computeWorldMatrix(registry, entity);  // 世界矩阵（含父链）
            renderable.visible = meshRenderer.visible;

            // 透明材质参数（opacity/alphaMode/cutoff → push constant）
            if (registry.all_of<VEngine::PBRMaterialComponent>(entity)) {
                auto& material = registry.get<VEngine::PBRMaterialComponent>(entity);
                renderable.materialParams = glm::vec4(
                    material.opacity,
                    static_cast<float>(material.alphaMode),
                    material.alphaCutoff, 0.0f);
                renderable.transparent = material.isTransparent();
            }
            // 世界位置到相机的距离（透明排序用）
            renderable.cameraDistance =
                glm::length(glm::vec3(renderable.modelMatrix[3]) - cameraPos);
            
            // 获取网格
            renderable.gpuMesh = MeshManager::getInstance().getMesh(meshRenderer.meshPath);
            if (!renderable.gpuMesh || !renderable.gpuMesh->isValid()) {
                continue;  // 跳过无效网格
            }
            
            // 获取纹理和材质ID
            std::string albedoPath, normalPath, metallicPath;
            
            if (registry.all_of<VEngine::PBRMaterialComponent>(entity)) {
                auto& material = registry.get<VEngine::PBRMaterialComponent>(entity);
                
                albedoPath = material.albedoMap;
                normalPath = material.normalMap;
                metallicPath = material.metallicMap;
                
                // Albedo 纹理：空路径使用默认白色
                if (!albedoPath.empty()) {
                    renderable.albedoTexture = TextureManager::getInstance().getTexture(albedoPath);
                } else {
                    renderable.albedoTexture = TextureManager::getInstance().getDefaultWhiteTexture();
                    albedoPath = "__default_white__";
                }
                
                // Normal 纹理：空路径使用默认法线 (0, 0, 1) 而不是白色(1, 1, 1)
                if (!normalPath.empty()) {
                    renderable.normalTexture = TextureManager::getInstance().getTexture(normalPath);
                } else {
                    renderable.normalTexture = TextureManager::getInstance().getDefaultNormalTexture();
                    normalPath = "__default_normal__";
                }
                
                // Metallic/Specular 纹理：空路径使用默认白色
                if (!metallicPath.empty()) {
                    renderable.specularTexture = TextureManager::getInstance().getTexture(metallicPath);
                } else {
                    // 使用默认纹理
                    renderable.albedoTexture = TextureManager::getInstance().getDefaultWhiteTexture();
                    renderable.normalTexture = TextureManager::getInstance().getDefaultNormalTexture();
                    renderable.specularTexture = TextureManager::getInstance().getDefaultWhiteTexture();
                    albedoPath = "__default_white__";
                    normalPath = "__default_normal__";
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
            
            // 遍历所有RenderPass，使用RTTI 判断类型并分配对应的材质描述符
            for (RenderPassBase* pass : renderPasses) {
                if (!pass) continue;
                
                // 检查纹理是否有数
                bool texturesValid = renderable.albedoTexture && renderable.normalTexture && renderable.specularTexture;
                if (!texturesValid) continue;
                
                // 使用 dynamic_cast 判断 Pass 类型
                if (ForwardPass* forwardPass = dynamic_cast<ForwardPass*>(pass)) {
                    allocateForwardPassDescriptor(renderable, forwardPass);
                }
                else if (GBufferPass* gbufferPass = dynamic_cast<GBufferPass*>(pass)) {
                    allocateGBufferPassDescriptor(renderable, gbufferPass);
                }
                // 可扩展其从Pass 类型...
            }
            
            renderable.valid = true;
            m_renderables.push_back(renderable);
        }
        
        // 避免每帧输出日志
        static size_t lastCount = 0;
        static size_t lastTransparentCount = 0;
        size_t transparentCount = 0;
        for (const auto& r : m_renderables) {
            if (r.transparent) transparentCount++;
        }
        if (m_renderables.size() != lastCount || transparentCount != lastTransparentCount) {
            std::cout << "[RenderSystem] Updated " << m_renderables.size() << " renderables ("
                      << transparentCount << " transparent)" << std::endl;
            lastCount = m_renderables.size();
            lastTransparentCount = transparentCount;
        }
    }
    
    /**
     * @brief 更新渲染数据（旧版兼容接口）
     * @param scene 要渲染的场景
     * @param forwardPass 用于分配材质描述符
     */
    void updateRenderables(VEngine::Scene* scene, ForwardPass* forwardPass) {
        std::vector<RenderPassBase*> passes;
        if (forwardPass) passes.push_back(forwardPass);
        updateRenderables(scene, passes);
    }
    
private:
    /**
     * @brief 与ForwardPass 分配材质描述符 (Pure RHI)
     */
    void allocateForwardPassDescriptor(RenderableEntity& renderable, ForwardPass* forwardPass) {
        // 尝试获取已有的材质描述符
        renderable.materialDescriptor = forwardPass->getMaterialDescriptor(renderable.materialId);
        
        // 如果不存在，分配新的
        if (!renderable.materialDescriptor) {
            renderable.materialDescriptor = forwardPass->allocateMaterialDescriptor(renderable.materialId);
            
            // 更新纹理绑定 (RHI)
            if (renderable.materialDescriptor) {
                forwardPass->updateMaterialTextures(
                    renderable.materialDescriptor,
                    renderable.albedoTexture->getTexture(),
                    renderable.albedoTexture->getSampler(),
                    renderable.normalTexture->getTexture(),
                    renderable.normalTexture->getSampler(),
                    renderable.specularTexture->getTexture(),
                    renderable.specularTexture->getSampler()
                );
            }
        }
    }
    
    /**
     * @brief 与GBufferPass 分配材质描述符 (Pure RHI)
     */
    void allocateGBufferPassDescriptor(RenderableEntity& renderable, GBufferPass* gbufferPass) {
        // 尝试获取已有的材质描述符
        renderable.gbufferMaterialDescriptor = gbufferPass->getMaterialDescriptor(renderable.materialId);
        
        // 如果不存在，分配新的
        if (!renderable.gbufferMaterialDescriptor) {
            renderable.gbufferMaterialDescriptor = gbufferPass->allocateMaterialDescriptor(renderable.materialId);
            
            // 更新纹理绑定 (RHI)
            if (renderable.gbufferMaterialDescriptor) {
                gbufferPass->updateMaterialTextures(
                    renderable.gbufferMaterialDescriptor,
                    renderable.albedoTexture->getTexture(),
                    renderable.albedoTexture->getSampler(),
                    renderable.normalTexture->getTexture(),
                    renderable.normalTexture->getSampler(),
                    renderable.specularTexture->getTexture(),
                    renderable.specularTexture->getSampler()
                );
            }
        }
    }
    
public:
    
    /**
     * @brief 统一渲染接口（使用RTTI 多态）— Pure RHI
     * 根据传入的RenderPass 类型自动调用对应的渲染逻辑
     * @param cmd RHI 命令缓冲
     * @param renderPass RenderPassBase 基类指针（支持ForwardPass、GBufferPass 等）
     * @param frameIndex 当前帧索引
     */
    void render(RHICommandBuffer* cmd, RenderPassBase* renderPass, uint32_t frameIndex) {
        if (!renderPass) return;

        // 使用 RTTI 判断 Pass 类型并调用对应的渲染逻辑
        if (ForwardPass* forwardPass = dynamic_cast<ForwardPass*>(renderPass)) {
            renderForwardPass(cmd, forwardPass, frameIndex);
        }
        else if (GBufferPass* gbufferPass = dynamic_cast<GBufferPass*>(renderPass)) {
            renderGBufferPass(cmd, gbufferPass, frameIndex);
        }
        // 可扩展其从Pass 类型...
    }

    /**
     * @brief 收集透明队列并按相机距离降序排序（远 → 近）
     * Forward 模式（ForwardPass 透明管线）与延迟模式（TransparentPass）共用
     */
    std::vector<const RenderableEntity*> getSortedTransparentList() const {
        std::vector<const RenderableEntity*> list;
        for (const auto& renderable : m_renderables) {
            if (renderable.valid && renderable.gpuMesh && renderable.transparent) {
                list.push_back(&renderable);
            }
        }
        std::sort(list.begin(), list.end(),
                  [](const RenderableEntity* a, const RenderableEntity* b) {
                      return a->cameraDistance > b->cameraDistance;
                  });
        return list;
    }

    /**
     * @brief 透明物体渲染（透明管线 + back-to-front 排序）
     * Forward 模式下由 renderForwardPass 内部调用；
     * GPU culling 间接绘制路径需在 opaque 之后单独调用。
     */
    void renderTransparent(RHICommandBuffer* cmd, ForwardPass* forwardPass, uint32_t frameIndex) {
        auto transparentList = getSortedTransparentList();
        if (transparentList.empty()) return;

        forwardPass->bindTransparentPipeline(cmd);
        for (const auto* renderable : transparentList) {
            if (renderable->materialDescriptor) {
                forwardPass->bindMaterialDescriptorSet(cmd, frameIndex, renderable->materialDescriptor);
            }
            forwardPass->pushModelMatrix(cmd, renderable->modelMatrix, renderable->materialParams);
            forwardPass->drawMesh(
                cmd,
                renderable->gpuMesh->getVertexBuffer(),
                renderable->gpuMesh->getIndexBuffer(),
                renderable->gpuMesh->getIndexCount()
            );
        }
    }

    /**
     * @brief 查询实体是否在透明队列（GPU culling 间接路径过滤用）
     */
    bool isTransparentEntity(entt::entity entity) const {
        for (const auto& renderable : m_renderables) {
            if (renderable.entityHandle == entity) return renderable.transparent;
        }
        return false;
    }
    
private:
    /**
     * @brief ForwardPass 渲染实现 (Pure RHI)
     * 两阶段：opaque（不透明管线，深度写开）→ transparent（透明管线，back-to-front）
     */
    void renderForwardPass(RHICommandBuffer* cmd, ForwardPass* forwardPass, uint32_t frameIndex) {
        // 绑定全局描述符集（Set 0: UBO） 只需绑定一次
        forwardPass->bindGlobalDescriptorSet(cmd, frameIndex);

        for (const auto& renderable : m_renderables) {
            if (!renderable.valid || !renderable.gpuMesh) continue;
            if (renderable.transparent) continue;   // 透明物体第二阶段绘制

            // 绑定材质描述符集（Set 1: 纹理） 每个实体独立的描述符
            if (renderable.materialDescriptor) {
                forwardPass->bindMaterialDescriptorSet(cmd, frameIndex, renderable.materialDescriptor);
            }

            // 推送模型矩阵 + 材质透明参数（Push Constants）
            forwardPass->pushModelMatrix(cmd, renderable.modelMatrix, renderable.materialParams);

            // 绘制网格
            forwardPass->drawMesh(
                cmd,
                renderable.gpuMesh->getVertexBuffer(),
                renderable.gpuMesh->getIndexBuffer(),
                renderable.gpuMesh->getIndexCount()
            );
        }

        // 透明阶段（无透明物体时无操作）
        renderTransparent(cmd, forwardPass, frameIndex);
    }

    /**
     * @brief GBufferPass 渲染实现 (Pure RHI)
     * 透明物体跳过（GBuffer 不支持混合；延迟模式透明由 TransparentPass 前向绘制）
     */
    void renderGBufferPass(RHICommandBuffer* cmd, GBufferPass* gbufferPass, uint32_t frameIndex) {
        // 绑定全局描述符集（Set 0: UBO） 只需绑定一次
        gbufferPass->bindGlobalDescriptorSet(cmd, frameIndex);

        for (const auto& renderable : m_renderables) {
            if (!renderable.valid || !renderable.gpuMesh) continue;
            if (renderable.transparent) continue;

            // 绑定材质描述符集（Set 1: 纹理） 每个实体独立的描述符
            if (renderable.gbufferMaterialDescriptor) {
                gbufferPass->bindMaterialDescriptorSet(cmd, frameIndex, renderable.gbufferMaterialDescriptor);
            }
            
            // 推送模型矩阵（Push Constants）
            gbufferPass->pushModelMatrix(cmd, renderable.modelMatrix);
            
            // 绘制网格
            gbufferPass->drawMesh(
                cmd,
                renderable.gpuMesh->getVertexBuffer(),
                renderable.gpuMesh->getIndexBuffer(),
                renderable.gpuMesh->getIndexCount()
            );
        }
    }
    
public:
    
    /**
     * @brief 获取可渲染实体列表（用于射线检测等：
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
     * @brief 获取 MeshManager（用于查试AABB 等）
     */
    MeshManager* getMeshManager() {
        return &MeshManager::getInstance();
    }
    
    /**
     * @brief 获取指定实体的GPUMesh（用于射线检测）
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
    RHIDevice* m_rhiDevice = nullptr;
    std::vector<RenderableEntity> m_renderables;
};

} // namespace VEngine
