/**
 * @file SceneSerializer.cpp
 * @brief 场景 JSON 序列化/反序列化实现
 */

#include "SceneSerializer.h"
#include "Entity.h"
#include "Components.h"
#include "SelectionManager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

namespace VEngine {

using json = nlohmann::json;

namespace {

// ============================================================
// JSON ↔ 基础类型辅助
// ============================================================

json vec3ToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

glm::vec3 jsonToVec3(const json& j, const glm::vec3& def = glm::vec3(0.0f)) {
    if (j.is_array() && j.size() >= 3) {
        return glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>());
    }
    return def;
}

const char* lightTypeToString(LightType t) {
    switch (t) {
        case LightType::Directional: return "Directional";
        case LightType::Spot: return "Spot";
        default: return "Point";
    }
}

LightType lightTypeFromString(const std::string& s) {
    if (s == "Directional") return LightType::Directional;
    if (s == "Spot") return LightType::Spot;
    return LightType::Point;
}

const char* alphaModeToString(AlphaMode m) {
    switch (m) {
        case AlphaMode::Mask: return "Mask";
        case AlphaMode::Blend: return "Blend";
        default: return "Opaque";
    }
}

AlphaMode alphaModeFromString(const std::string& s) {
    if (s == "Mask") return AlphaMode::Mask;
    if (s == "Blend") return AlphaMode::Blend;
    return AlphaMode::Opaque;
}

// ============================================================
// 组件序列化
// ============================================================

json serializeTransform(const TransformComponent& tx) {
    return json{
        {"position", vec3ToJson(tx.position)},
        {"rotation", vec3ToJson(tx.rotation)},
        {"scale",    vec3ToJson(tx.scale)},
    };
}

json serializeMeshRenderer(const MeshRendererComponent& mr) {
    return json{
        {"meshPath", mr.meshPath},
        {"materialPath", mr.materialPath},
        {"castShadows", mr.castShadows},
        {"receiveShadows", mr.receiveShadows},
        {"visible", mr.visible},
    };
}

json serializePBRMaterial(const PBRMaterialComponent& m) {
    return json{
        {"albedo", vec3ToJson(m.albedo)},
        {"metallic", m.metallic},
        {"roughness", m.roughness},
        {"ao", m.ao},
        {"emissive", vec3ToJson(m.emissive)},
        {"emissiveStrength", m.emissiveStrength},
        {"opacity", m.opacity},
        {"alphaMode", alphaModeToString(m.alphaMode)},
        {"alphaCutoff", m.alphaCutoff},
        {"albedoMap", m.albedoMap},
        {"normalMap", m.normalMap},
        {"metallicMap", m.metallicMap},
        {"roughnessMap", m.roughnessMap},
        {"aoMap", m.aoMap},
        {"emissiveMap", m.emissiveMap},
    };
}

json serializeLight(const LightComponent& l) {
    return json{
        {"type", lightTypeToString(l.type)},
        {"color", vec3ToJson(l.color)},
        {"intensity", l.intensity},
        {"range", l.range},
        {"constantAttenuation", l.constantAttenuation},
        {"linearAttenuation", l.linearAttenuation},
        {"quadraticAttenuation", l.quadraticAttenuation},
        {"innerConeAngle", l.innerConeAngle},
        {"outerConeAngle", l.outerConeAngle},
        {"castShadows", l.castShadows},
        {"shadowBias", l.shadowBias},
        {"shadowMapResolution", l.shadowMapResolution},
    };
}

json serializeCamera(const CameraComponent& c) {
    return json{
        {"projectionType", c.projectionType == ProjectionType::Orthographic ? "Orthographic" : "Perspective"},
        {"isPrimary", c.isPrimary},
        {"fixedAspectRatio", c.fixedAspectRatio},
        {"fov", c.fov},
        {"aspectRatio", c.aspectRatio},
        {"nearClip", c.nearClip},
        {"farClip", c.farClip},
        {"orthographicSize", c.orthographicSize},
        {"orthographicNear", c.orthographicNear},
        {"orthographicFar", c.orthographicFar},
    };
}

// ============================================================
// 组件反序列化
// ============================================================

void deserializeTransform(const json& j, TransformComponent& tx) {
    if (j.contains("position")) tx.position = jsonToVec3(j["position"], tx.position);
    if (j.contains("rotation")) tx.rotation = jsonToVec3(j["rotation"], tx.rotation);
    if (j.contains("scale")) tx.scale = jsonToVec3(j["scale"], tx.scale);
}

void deserializeMeshRenderer(const json& j, MeshRendererComponent& mr) {
    mr.meshPath = j.value("meshPath", mr.meshPath);
    mr.materialPath = j.value("materialPath", mr.materialPath);
    mr.castShadows = j.value("castShadows", mr.castShadows);
    mr.receiveShadows = j.value("receiveShadows", mr.receiveShadows);
    mr.visible = j.value("visible", mr.visible);
}

void deserializePBRMaterial(const json& j, PBRMaterialComponent& m) {
    m.albedo = jsonToVec3(j.value("albedo", json::array()), m.albedo);
    m.metallic = j.value("metallic", m.metallic);
    m.roughness = j.value("roughness", m.roughness);
    m.ao = j.value("ao", m.ao);
    m.emissive = jsonToVec3(j.value("emissive", json::array()), m.emissive);
    m.emissiveStrength = j.value("emissiveStrength", m.emissiveStrength);
    m.opacity = j.value("opacity", m.opacity);
    m.alphaMode = alphaModeFromString(j.value("alphaMode", std::string("Opaque")));
    m.alphaCutoff = j.value("alphaCutoff", m.alphaCutoff);
    m.albedoMap = j.value("albedoMap", m.albedoMap);
    m.normalMap = j.value("normalMap", m.normalMap);
    m.metallicMap = j.value("metallicMap", m.metallicMap);
    m.roughnessMap = j.value("roughnessMap", m.roughnessMap);
    m.aoMap = j.value("aoMap", m.aoMap);
    m.emissiveMap = j.value("emissiveMap", m.emissiveMap);
}

void deserializeLight(const json& j, LightComponent& l) {
    l.type = lightTypeFromString(j.value("type", "Point"));
    l.color = jsonToVec3(j.value("color", json::array()), l.color);
    l.intensity = j.value("intensity", l.intensity);
    l.range = j.value("range", l.range);
    l.constantAttenuation = j.value("constantAttenuation", l.constantAttenuation);
    l.linearAttenuation = j.value("linearAttenuation", l.linearAttenuation);
    l.quadraticAttenuation = j.value("quadraticAttenuation", l.quadraticAttenuation);
    l.innerConeAngle = j.value("innerConeAngle", l.innerConeAngle);
    l.outerConeAngle = j.value("outerConeAngle", l.outerConeAngle);
    l.castShadows = j.value("castShadows", l.castShadows);
    l.shadowBias = j.value("shadowBias", l.shadowBias);
    l.shadowMapResolution = j.value("shadowMapResolution", l.shadowMapResolution);
}

void deserializeCamera(const json& j, CameraComponent& c) {
    c.projectionType = (j.value("projectionType", "Perspective") == "Orthographic")
        ? ProjectionType::Orthographic : ProjectionType::Perspective;
    c.isPrimary = j.value("isPrimary", c.isPrimary);
    c.fixedAspectRatio = j.value("fixedAspectRatio", c.fixedAspectRatio);
    c.fov = j.value("fov", c.fov);
    c.aspectRatio = j.value("aspectRatio", c.aspectRatio);
    c.nearClip = j.value("nearClip", c.nearClip);
    c.farClip = j.value("farClip", c.farClip);
    c.orthographicSize = j.value("orthographicSize", c.orthographicSize);
    c.orthographicNear = j.value("orthographicNear", c.orthographicNear);
    c.orthographicFar = j.value("orthographicFar", c.orthographicFar);
}

} // anonymous namespace

// ============================================================
// 序列化
// ============================================================

bool SceneSerializer::serialize(const std::string& filePath) const {
    if (!m_scene) return false;

    auto& registry = m_scene->m_registry;

    // 按 Hierarchy 顺序收集实体（根→子深度优先），保证兄弟顺序确定
    std::vector<entt::entity> ordered;
    std::set<entt::entity> visited;
    std::function<void(entt::entity)> visit = [&](entt::entity e) {
        if (visited.count(e)) return;   // 防御损坏的链表成环
        visited.insert(e);
        ordered.push_back(e);
        auto* rel = registry.try_get<RelationshipComponent>(e);
        if (!rel) return;
        entt::entity c = rel->firstChild;
        int guard = 0;
        while (c != entt::null && guard++ < 100000) {
            visit(c);
            auto* crel = registry.try_get<RelationshipComponent>(c);
            c = (crel && crel->nextSibling != c) ? crel->nextSibling : entt::null;
        }
    };
    for (auto& root : m_scene->getRootEntities()) {
        visit(root.getHandle());
    }
    // 兜底：不在层级链中的游离实体也保存
    for (auto e : registry.view<UUIDComponent>()) {
        if (!visited.count(e)) ordered.push_back(e);
    }

    json entities = json::array();
    for (entt::entity e : ordered) {
        auto& uuidComp = registry.get<UUIDComponent>(e);
        json j{
            {"uuid", uuidComp.uuid},
            {"tag", registry.get<TagComponent>(e).tag},
        };

        if (auto* tx = registry.try_get<TransformComponent>(e)) {
            j["transform"] = serializeTransform(*tx);
        }
        if (auto* rel = registry.try_get<RelationshipComponent>(e)) {
            uint64_t parentUUID = 0;   // 0 = 根
            if (rel->parent != entt::null && registry.all_of<UUIDComponent>(rel->parent)) {
                parentUUID = registry.get<UUIDComponent>(rel->parent).uuid;
            }
            j["parent"] = parentUUID;
        }
        if (auto* mr = registry.try_get<MeshRendererComponent>(e)) {
            j["meshRenderer"] = serializeMeshRenderer(*mr);
        }
        if (auto* mat = registry.try_get<PBRMaterialComponent>(e)) {
            j["pbrMaterial"] = serializePBRMaterial(*mat);
        }
        if (auto* light = registry.try_get<LightComponent>(e)) {
            j["light"] = serializeLight(*light);
        }
        if (auto* cam = registry.try_get<CameraComponent>(e)) {
            j["camera"] = serializeCamera(*cam);
        }
        entities.push_back(std::move(j));
    }

    json root{
        {"format", "vscene"},
        {"version", 1},
        {"scene", {
            {"name", m_scene->m_name},
            {"nextUUID", m_scene->m_nextUUID},
        }},
        {"entities", std::move(entities)},
    };

    // 父目录不存在时自动创建（如 assets/scenes/ 首次保存）
    if (auto parent = fs::path(filePath).parent_path(); !parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);   // 已存在时无副作用
    }

    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "[SceneSerializer] Cannot open file for writing: " << filePath << "\n";
        return false;
    }
    ofs << root.dump(2) << std::endl;
    if (!ofs.good()) {
        std::cerr << "[SceneSerializer] Write failed: " << filePath << "\n";
        return false;
    }

    std::cout << "[SceneSerializer] Saved '" << m_scene->m_name << "' ("
              << ordered.size() << " entities) -> " << filePath << "\n";
    return true;
}

// ============================================================
// 反序列化
// ============================================================

bool SceneSerializer::deserialize(const std::string& filePath) {
    if (!m_scene) return false;

    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "[SceneSerializer] Cannot open file: " << filePath << "\n";
        return false;
    }

    json root;
    try {
        ifs >> root;
    } catch (const json::parse_error& e) {
        std::cerr << "[SceneSerializer] JSON parse error in '" << filePath << "': " << e.what() << "\n";
        return false;
    }

    if (!root.contains("entities") || !root["entities"].is_array()) {
        std::cerr << "[SceneSerializer] Invalid scene file (missing 'entities' array): " << filePath << "\n";
        return false;
    }

    // 清空现有场景（复用 Scene 对象；选中状态同步清除）
    m_scene->clear();
    SelectionManager::getInstance().clearSelection();

    const auto& sceneInfo = root.value("scene", json::object());
    m_scene->m_name = sceneInfo.value("name", std::string("Untitled"));
    if (sceneInfo.contains("nextUUID")) {
        m_scene->m_nextUUID = sceneInfo["nextUUID"].get<uint64_t>();
    }

    std::unordered_map<uint64_t, entt::entity> uuidToEntity;
    uint64_t maxUUID = 0;

    // Pass 1: 创建实体 + 组件（文件顺序即 Hierarchy 顺序，父先于子）
    for (const auto& j : root["entities"]) {
        uint64_t uuid = j.value("uuid", uint64_t(0));
        std::string tag = j.value("tag", std::string("Entity"));

        Entity entity = m_scene->createEntityWithUUID(uuid, tag);
        uuidToEntity[uuid] = entity.getHandle();
        maxUUID = std::max(maxUUID, uuid);

        if (j.contains("transform")) {
            deserializeTransform(j["transform"], entity.getComponent<TransformComponent>());
        }
        if (j.contains("meshRenderer")) {
            deserializeMeshRenderer(j["meshRenderer"],
                entity.addComponent<MeshRendererComponent>());
        }
        if (j.contains("pbrMaterial")) {
            deserializePBRMaterial(j["pbrMaterial"],
                entity.addComponent<PBRMaterialComponent>());
        }
        if (j.contains("light")) {
            deserializeLight(j["light"], entity.addComponent<LightComponent>());
        }
        if (j.contains("camera")) {
            deserializeCamera(j["camera"], entity.addComponent<CameraComponent>());
        }
    }

    // Pass 2: 重建父子关系（UUID 引用 → setParent，保持兄弟顺序）
    for (const auto& j : root["entities"]) {
        if (!j.contains("parent")) continue;
        uint64_t parentUUID = j.value("parent", uint64_t(0));
        if (parentUUID == 0) continue;

        auto childIt = uuidToEntity.find(j.value("uuid", uint64_t(0)));
        auto parentIt = uuidToEntity.find(parentUUID);
        if (childIt == uuidToEntity.end() || parentIt == uuidToEntity.end()) {
            std::cout << "[SceneSerializer] Warning: dangling parent reference ("
                      << parentUUID << ") in '" << filePath << "'\n";
            continue;
        }
        Entity child(childIt->second, m_scene);
        Entity parent(parentIt->second, m_scene);
        if (child != parent) child.setParent(parent);
    }

    // nextUUID 兜底：至少比已用的最大 UUID 大 1
    m_scene->m_nextUUID = std::max(m_scene->m_nextUUID, maxUUID + 1);

    std::cout << "[SceneSerializer] Loaded '" << m_scene->m_name << "' ("
              << uuidToEntity.size() << " entities) <- " << filePath << "\n";
    return true;
}

} // namespace VEngine
