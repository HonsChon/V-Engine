#pragma once

#include "Scene.h"
#include "Entity.h"
#include "Components.h"
#include <map>
#include <memory>
#include <string>

class Mesh;

namespace tinygltf { class Model; }

namespace VEngine {

/**
 * @brief 模型文件导入器
 *
 * 统一的模型导入入口：
 *  - OBJ  (.obj)       单实体 + .mtl 材质解析（几何合并为单 Mesh，与默认场景行为一致）
 *  - glTF (.gltf/.glb) 保留节点层级（glTF 场景图 → ECS 父子实体树），
 *                       每个 mesh primitive 对应一个 MeshRendererComponent
 *
 * glTF meshId 约定："<文件路径>#<meshIndex>_<primitiveIndex>"
 * （MeshManager 惰性加载时按此约定解析，见 MeshManager::loadMesh）
 *
 * 已知限制：
 *  - .glb 内嵌贴图无法提取路径（tinygltf 不解码像素），使用默认材质贴图
 *  - glTF metallicRoughness 打包贴图近似映射到 metallicMap
 *    （G=roughness / B=metallic 通道拆分暂不支持）
 *  - glTF 不做 centerAndNormalize（会破坏节点层级的原始坐标）
 */
class ModelImporter {
public:
    /**
     * @brief 判断文件是否为支持的模型格式
     */
    static bool isSupported(const std::string& filePath);

    /**
     * @brief 导入模型文件到场景
     * @param scene 目标场景
     * @param filePath 模型文件路径（.obj/.gltf/.glb）
     * @return 创建的根实体（失败返回无效实体）
     */
    static Entity importModel(Scene* scene, const std::string& filePath);

    /**
     * @brief 解析 glTF 文件，提取全部 mesh primitive
     *
     * 供 MeshManager 惰性加载使用（场景反序列化后 meshId 不在缓存时）。
     * 一次解析整个文件，返回所有 primitive，由调用方逐个注册。
     * @param filePath glTF 文件路径
     * @param err 输出错误信息
     * @return meshId 后缀（"#meshIdx_primIdx"）→ Mesh 映射，失败为空
     */
    static std::map<std::string, std::shared_ptr<Mesh>> extractGLTFMeshes(
        const std::string& filePath, std::string& err);

private:
    static Entity importOBJ(Scene* scene, const std::string& filePath, const std::string& stem);
    static Entity importGLTF(Scene* scene, const std::string& filePath, const std::string& stem);

    /// 递归为 glTF node（及其子树）创建实体
    static void addNodeEntity(Scene* scene, const tinygltf::Model& model,
                              int nodeIdx, Entity parent,
                              const std::string& baseDir, const std::string& meshPrefix);
};

} // namespace VEngine
