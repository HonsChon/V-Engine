#pragma once

#include "Scene.h"
#include <string>

namespace VEngine {

/**
 * @brief 场景序列化器 — JSON 格式（.vscene）
 *
 * 设计要点：
 *  - 父子关系用 UUID 引用（"parent": <uuid>，0 = 根），
 *    entt 句柄不序列化；加载两遍：先建实体+组件，再重建 setParent
 *  - 实体按 Hierarchy 顺序（根→子深度优先）保存，保证兄弟顺序确定
 *  - deserialize 复用同一个 Scene 对象、只清空 registry（Scene::clear），
 *    Engine/面板/SelectionManager 的指针无需重挂
 *  - 路径字符串原样保存；重开时的路径兜底解析见 AssetPath.h
 *
 * 序列化组件范围：Tag/Transform/Relationship(经 parent UUID)/
 * MeshRenderer/PBRMaterial/Light/Camera（预留组件暂不序列化）
 */
class SceneSerializer {
public:
    explicit SceneSerializer(Scene* scene) : m_scene(scene) {}

    /**
     * @brief 序列化场景到文件
     * @param filePath 目标路径（.vscene / .json）
     * @return 是否成功
     */
    bool serialize(const std::string& filePath) const;

    /**
     * @brief 从文件反序列化（清空现有场景后重建）
     * @param filePath 场景文件路径
     * @return 是否成功（失败时场景保持原状或为空）
     */
    bool deserialize(const std::string& filePath);

private:
    Scene* m_scene = nullptr;
};

} // namespace VEngine
