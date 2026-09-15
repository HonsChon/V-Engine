/**
 * @file ModelImporter.cpp
 * @brief 模型文件导入实现（OBJ 材质 + glTF 节点层级）
 */

#include "ModelImporter.h"
#include "AssetPath.h"
#include "Mesh.h"
#include "MeshManager.h"

#include "tiny_obj_loader.h"   // 实现宏已在 Mesh.cpp 定义
#include "tiny_gltf.h"         // TINYGLTF_NO_STB_IMAGE 由 CMake 全局定义

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace VEngine {

// ============================================================
// 路径解析（AssetPath.h 接口的实现）
// ============================================================

static bool isPresetId(const std::string& path) {
    if (path == "sphere" || path == "cube" || path == "plane") return true;
    if (path.rfind("__", 0) == 0) return true;              // __default_white__ 等内部资源
    if (path.find('|') != std::string::npos) return true;   // 材质描述符 ID
    return false;
}

std::string resolveAssetPath(const std::string& path) {
    if (path.empty() || isPresetId(path)) return path;

    // 拆出 glTF meshId 的 "#mesh_prim" 后缀
    std::string base = path;
    std::string suffix;
    auto hash = path.find('#');
    if (hash != std::string::npos) {
        base = path.substr(0, hash);
        suffix = path.substr(hash);
    }

    std::string resolved = base;
    std::error_code ec;
    if (!fs::exists(base, ec)) {
        // 定位 "assets/" 起始的相对部分，按不同运行目录深度重试
        auto pos = base.find("assets/");
        if (pos != std::string::npos) {
            std::string rel = base.substr(pos);
            const std::string candidates[] = {
                rel, "../../" + rel, "../../../" + rel, "../" + rel
            };
            for (const auto& cand : candidates) {
                if (fs::exists(cand, ec)) {
                    resolved = cand;
                    break;
                }
            }
        }
    }
    return resolved + suffix;
}

std::string joinPath(const std::string& baseDir, const std::string& path) {
    if (path.empty()) return path;
    if (path.rfind("data:", 0) == 0) return "";   // data URI 无法落盘使用
    fs::path p(path);
    if (p.is_absolute()) return path;
    if (baseDir.empty()) return path;
    return (fs::path(baseDir) / p).lexically_normal().generic_string();
}

// ============================================================
// glTF 内部工具
// ============================================================

namespace {

/// 访问器视图：原始字节 + 元素步长（处理 interleaved 布局的 byteStride）
struct AccessorView {
    const unsigned char* data = nullptr;
    size_t count = 0;
    size_t stride = 0;   // 相邻元素的字节间隔
    int componentType = -1;
    int numComponents = 0;
};

size_t gltfComponentSize(int componentType) {
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return 1;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT: return 4;
        default: return 0;
    }
}

size_t gltfNumComponents(int type) {
    switch (type) {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2: return 2;
        case TINYGLTF_TYPE_VEC3: return 3;
        case TINYGLTF_TYPE_VEC4: return 4;
        case TINYGLTF_TYPE_MAT2: return 4;
        case TINYGLTF_TYPE_MAT3: return 9;
        case TINYGLTF_TYPE_MAT4: return 16;
        default: return 0;
    }
}

AccessorView getAccessorView(const tinygltf::Model& model, int accessorIdx) {
    AccessorView v;
    if (accessorIdx < 0 || static_cast<size_t>(accessorIdx) >= model.accessors.size()) return v;
    const auto& accessor = model.accessors[accessorIdx];
    if (accessor.bufferView < 0 || static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size()) return v;
    const auto& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || static_cast<size_t>(view.buffer) >= model.buffers.size()) return v;
    const auto& buffer = model.buffers[view.buffer];
    if (buffer.data.empty()) return v;

    size_t elemSize = gltfComponentSize(accessor.componentType) * gltfNumComponents(accessor.type);
    if (elemSize == 0) return v;

    v.data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    v.count = accessor.count;
    v.stride = (view.byteStride > 0) ? static_cast<size_t>(view.byteStride) : elemSize;
    v.componentType = accessor.componentType;
    v.numComponents = static_cast<int>(gltfNumComponents(accessor.type));
    return v;
}

/// 解析 glTF 文件（.glb 二进制 / .gltf ASCII 自动分发）
bool loadGLTFFile(const std::string& filePath, tinygltf::Model& model, std::string& err) {
    tinygltf::TinyGLTF loader;
    std::string warn;

    // 引擎只需要贴图 URI 路径，不解码像素；
    // 返回 true 以跳过 .glb 内嵌贴图（bufferView）的解码（否则整个加载失败）
    loader.SetImageLoader(
        [](tinygltf::Image*, const int, std::string*, std::string*,
           int, int, const unsigned char*, int, void*) -> bool { return true; },
        nullptr);

    std::string ext = fs::path(filePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    bool ok = (ext == ".glb")
        ? loader.LoadBinaryFromFile(&model, &err, &warn, filePath)
        : loader.LoadASCIIFromFile(&model, &err, &warn, filePath);

    if (!warn.empty()) std::cout << "[ModelImporter] glTF warning: " << warn << "\n";
    if (!ok) {
        err = err.empty() ? "unknown parse error" : err;
        return false;
    }
    return true;
}

/// 从单个 primitive 构建 Mesh（不依赖 MeshManager，纯几何提取）
std::shared_ptr<Mesh> buildPrimitiveMesh(const tinygltf::Model& model, const tinygltf::Primitive& prim) {
    auto posIt = prim.attributes.find("POSITION");
    if (posIt == prim.attributes.end()) return nullptr;

    AccessorView posView = getAccessorView(model, posIt->second);
    if (!posView.data || posView.numComponents != 3) return nullptr;
    size_t vertexCount = posView.count;
    if (vertexCount == 0) return nullptr;

    std::vector<Vertex> vertices(vertexCount);
    bool hasNormals = false;
    bool hasTexCoords = false;
    bool hasTangents = false;

    for (size_t i = 0; i < vertexCount; ++i) {
        const float* p = reinterpret_cast<const float*>(posView.data + i * posView.stride);
        vertices[i].pos = glm::vec3(p[0], p[1], p[2]);
    }

    if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end()) {
        AccessorView v = getAccessorView(model, it->second);
        if (v.data && v.numComponents == 3) {
            hasNormals = true;
            for (size_t i = 0; i < vertexCount; ++i) {
                const float* n = reinterpret_cast<const float*>(v.data + i * v.stride);
                vertices[i].normal = glm::vec3(n[0], n[1], n[2]);
            }
        }
    }

    if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end()) {
        AccessorView v = getAccessorView(model, it->second);
        if (v.data && v.numComponents == 2) {
            hasTexCoords = true;
            for (size_t i = 0; i < vertexCount; ++i) {
                const float* t = reinterpret_cast<const float*>(v.data + i * v.stride);
                // glTF UV 原点在左上，与 Vulkan/DX12 采样约定一致，无需翻转 V（OBJ 才需要）
                vertices[i].texCoord = glm::vec2(t[0], t[1]);
            }
        }
    }

    if (auto it = prim.attributes.find("TANGENT"); it != prim.attributes.end()) {
        AccessorView v = getAccessorView(model, it->second);
        if (v.data && v.numComponents == 4) {
            hasTangents = true;
            for (size_t i = 0; i < vertexCount; ++i) {
                const float* t = reinterpret_cast<const float*>(v.data + i * v.stride);
                vertices[i].tangent = glm::vec3(t[0], t[1], t[2]);
            }
        }
    }

    // 索引（缺失时顺序生成）
    std::vector<uint32_t> indices;
    if (prim.indices >= 0) {
        AccessorView v = getAccessorView(model, prim.indices);
        if (v.data) {
            indices.reserve(v.count);
            for (size_t i = 0; i < v.count; ++i) {
                const unsigned char* e = v.data + i * v.stride;
                uint32_t idx = 0;
                switch (v.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        idx = *reinterpret_cast<const uint8_t*>(e); break;
                    case TINYGLTF_COMPONENT_TYPE_BYTE:
                        idx = static_cast<uint32_t>(*reinterpret_cast<const int8_t*>(e)); break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        idx = *reinterpret_cast<const uint16_t*>(e); break;
                    case TINYGLTF_COMPONENT_TYPE_SHORT:
                        idx = static_cast<uint32_t>(*reinterpret_cast<const int16_t*>(e)); break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    case TINYGLTF_COMPONENT_TYPE_FLOAT:
                        idx = *reinterpret_cast<const uint32_t*>(e); break;
                    default: break;
                }
                if (idx >= vertexCount) return nullptr;   // 索引越界，文件损坏
                indices.push_back(idx);
            }
        }
    } else {
        indices.resize(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) indices[i] = static_cast<uint32_t>(i);
    }

    (void)hasTangents;
    (void)hasTexCoords;

    auto mesh = std::make_shared<Mesh>();
    mesh->setVertices(vertices);       // 内部计算包围盒
    mesh->setIndices(indices);
    if (!hasNormals) mesh->calculateNormals();
    if (!hasTangents) mesh->calculateTangents();
    return mesh;
}

/// 收集全部 mesh primitive，key 为 "#meshIdx_primIdx"
void collectGLTFMeshes(const tinygltf::Model& model, std::map<std::string, std::shared_ptr<Mesh>>& out) {
    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        const auto& gltfMesh = model.meshes[mi];
        for (size_t pi = 0; pi < gltfMesh.primitives.size(); ++pi) {
            auto mesh = buildPrimitiveMesh(model, gltfMesh.primitives[pi]);
            if (!mesh) continue;
            std::string baseName = gltfMesh.name.empty()
                ? ("mesh_" + std::to_string(mi)) : gltfMesh.name;
            mesh->setName(baseName + "_" + std::to_string(pi));
            out["#" + std::to_string(mi) + "_" + std::to_string(pi)] = mesh;
        }
    }
}

/// glTF 材质 → PBRMaterialComponent（贴图路径解析为 glTF 文件所在目录的相对路径）
PBRMaterialComponent buildMaterialFromGLTF(const tinygltf::Model& model, int materialIdx, const std::string& baseDir) {
    PBRMaterialComponent mat;
    if (materialIdx < 0 || static_cast<size_t>(materialIdx) >= model.materials.size()) return mat;
    const auto& m = model.materials[materialIdx];
    const auto& pbr = m.pbrMetallicRoughness;

    mat.albedo = glm::vec3(
        static_cast<float>(pbr.baseColorFactor[0]),
        static_cast<float>(pbr.baseColorFactor[1]),
        static_cast<float>(pbr.baseColorFactor[2]));
    mat.metallic = static_cast<float>(pbr.metallicFactor);
    mat.roughness = static_cast<float>(pbr.roughnessFactor);
    if (pbr.baseColorFactor.size() >= 4) {
        mat.opacity = static_cast<float>(pbr.baseColorFactor[3]);
    }
    if (m.emissiveFactor.size() == 3) {
        mat.emissive = glm::vec3(
            static_cast<float>(m.emissiveFactor[0]),
            static_cast<float>(m.emissiveFactor[1]),
            static_cast<float>(m.emissiveFactor[2]));
    }

    // alpha 模式: "MASK" → alpha 测试, "BLEND" → 半透明混合
    if (m.alphaMode == "MASK") {
        mat.alphaMode = AlphaMode::Mask;
        mat.alphaCutoff = static_cast<float>(m.alphaCutoff);
    } else if (m.alphaMode == "BLEND") {
        mat.alphaMode = AlphaMode::Blend;
    }

    auto texPath = [&](int texIdx) -> std::string {
        if (texIdx < 0 || static_cast<size_t>(texIdx) >= model.textures.size()) return "";
        const auto& tex = model.textures[texIdx];
        if (tex.source < 0 || static_cast<size_t>(tex.source) >= model.images.size()) return "";
        const auto& img = model.images[tex.source];
        if (img.uri.empty()) return "";   // .glb 内嵌贴图 — 已知限制
        return joinPath(baseDir, img.uri);
    };

    mat.albedoMap = texPath(pbr.baseColorTexture.index);
    mat.normalMap = texPath(m.normalTexture.index);
    // 近似：metallicRoughness 打包贴图（G=roughness, B=metallic）直接按 metallicMap 使用
    mat.metallicMap = texPath(pbr.metallicRoughnessTexture.index);
    mat.emissiveMap = texPath(m.emissiveTexture.index);
    return mat;
}

/// .mtl 材质 → PBRMaterialComponent
PBRMaterialComponent buildMaterialFromOBJ(const tinyobj::material_t& m, const std::string& mtlDir) {
    PBRMaterialComponent mat;
    mat.albedo = glm::vec3(m.diffuse[0], m.diffuse[1], m.diffuse[2]);
    if (m.shininess > 0.0f) {
        // MTL 高光指数(Ns 0~1000) → 粗糙度近似映射
        mat.roughness = std::clamp(1.0f - static_cast<float>(m.shininess) / 1000.0f, 0.03f, 1.0f);
    }
    // MTL 溶解度 d (1=不透明, 0=全透明) → opacity + Blend 模式
    if (m.dissolve < 1.0f) {
        mat.opacity = std::clamp(static_cast<float>(m.dissolve), 0.0f, 1.0f);
        mat.alphaMode = AlphaMode::Blend;
    }
    auto map = [&](const std::string& texname) {
        return texname.empty() ? std::string() : joinPath(mtlDir, texname);
    };
    mat.albedoMap = map(m.diffuse_texname);
    if (!m.normal_texname.empty()) mat.normalMap = map(m.normal_texname);
    else if (!m.bump_texname.empty()) mat.normalMap = map(m.bump_texname);
    mat.metallicMap = map(m.specular_texname);
    return mat;
}

} // anonymous namespace

// ============================================================
// 公开接口
// ============================================================

bool ModelImporter::isSupported(const std::string& filePath) {
    std::string ext = fs::path(filePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".obj" || ext == ".gltf" || ext == ".glb";
}

std::map<std::string, std::shared_ptr<Mesh>> ModelImporter::extractGLTFMeshes(
    const std::string& filePath, std::string& err) {
    std::map<std::string, std::shared_ptr<Mesh>> result;
    std::string resolved = resolveAssetPath(filePath);

    tinygltf::Model model;
    if (!loadGLTFFile(resolved, model, err)) {
        std::cerr << "[ModelImporter] Failed to load glTF '" << filePath << "': " << err << "\n";
        return result;
    }
    collectGLTFMeshes(model, result);
    return result;
}

Entity ModelImporter::importModel(Scene* scene, const std::string& filePath) {
    if (!scene || filePath.empty()) return Entity();

    std::string ext = fs::path(filePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    std::string stem = fs::path(filePath).stem().string();

    Entity root;
    if (ext == ".obj") {
        root = importOBJ(scene, filePath, stem);
    } else if (ext == ".gltf" || ext == ".glb") {
        root = importGLTF(scene, filePath, stem);
    } else {
        std::cerr << "[ModelImporter] Unsupported format: " << filePath << "\n";
    }
    return root;
}

// ============================================================
// OBJ 导入
// ============================================================

Entity ModelImporter::importOBJ(Scene* scene, const std::string& filePath, const std::string& stem) {
    std::string resolved = resolveAssetPath(filePath);

    // 几何：走 MeshManager 缓存（loadFromOBJ + centerAndNormalize，与默认场景行为一致）
    auto gpuMesh = MeshManager::getInstance().getMesh(resolved);
    if (!gpuMesh) {
        std::cerr << "[ModelImporter] Failed to load OBJ: " << filePath << "\n";
        return Entity();
    }

    auto entity = scene->createEntity(stem);
    entity.addComponent<MeshRendererComponent>(resolved, "default_material");

    // 材质：轻量二次解析 .obj 仅提取 .mtl（几何已由 MeshManager 缓存）
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    std::string mtlDir = fs::path(resolved).parent_path().string();

    if (tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, resolved.c_str(), mtlDir.c_str())) {
        int matIdx = -1;
        for (const auto& shape : shapes) {
            for (int id : shape.mesh.material_ids) {
                if (id >= 0 && static_cast<size_t>(id) < materials.size()) {
                    matIdx = id;
                    break;
                }
            }
            if (matIdx >= 0) break;
        }
        if (matIdx >= 0) {
            entity.addComponent<PBRMaterialComponent>(buildMaterialFromOBJ(materials[matIdx], mtlDir));
        }
    } else if (!err.empty()) {
        std::cout << "[ModelImporter] OBJ material parse skipped: " << err << "\n";
    }

    std::cout << "[ModelImporter] Imported OBJ '" << stem << "'"
              << " (vertices: " << gpuMesh->getVertexCount()
              << ", material: " << (entity.hasComponent<PBRMaterialComponent>() ? "mtl" : "default") << ")\n";
    return entity;
}

// ============================================================
// glTF 导入
// ============================================================

void ModelImporter::addNodeEntity(Scene* scene, const tinygltf::Model& model,
                                  int nodeIdx, Entity parent,
                                  const std::string& baseDir, const std::string& meshPrefix) {
    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= model.nodes.size()) return;
    const auto& node = model.nodes[nodeIdx];

    std::string name = node.name.empty() ? ("Node " + std::to_string(nodeIdx)) : node.name;
    Entity entity = scene->createEntity(name);
    if (parent) entity.setParent(parent);

    // 变换：matrix（列主序）或 TRS → 欧拉角 TransformComponent
    auto& tx = entity.getComponent<TransformComponent>();
    if (node.matrix.size() == 16) {
        glm::mat4 m = glm::make_mat4(node.matrix.data());
        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        if (glm::decompose(m, scale, rotation, translation, skew, perspective)) {
            tx.position = translation;
            tx.rotation = glm::eulerAngles(rotation);
            tx.scale = scale;
        }
    } else {
        if (node.translation.size() == 3) {
            tx.position = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
        }
        if (node.rotation.size() == 4) {
            // glTF 四元数 (x,y,z,w) → glm 构造 (w,x,y,z)
            glm::quat q(static_cast<float>(node.rotation[3]),
                        static_cast<float>(node.rotation[0]),
                        static_cast<float>(node.rotation[1]),
                        static_cast<float>(node.rotation[2]));
            tx.rotation = glm::eulerAngles(q);
        }
        if (node.scale.size() == 3) {
            tx.scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
        }
    }

    // 网格：每个 primitive 一个 MeshRenderer；实体只能持有一个，
    // 额外 primitive 落成 "part" 子实体（保留节点层级 + 独立材质）
    if (node.mesh >= 0 && static_cast<size_t>(node.mesh) < model.meshes.size()) {
        const auto& gltfMesh = model.meshes[node.mesh];
        for (size_t pi = 0; pi < gltfMesh.primitives.size(); ++pi) {
            const auto& prim = gltfMesh.primitives[pi];
            std::string meshId = meshPrefix + "#" + std::to_string(node.mesh) + "_" + std::to_string(pi);

            Entity target = entity;
            if (pi > 0) {
                target = scene->createEntity(name + " (part " + std::to_string(pi) + ")");
                target.setParent(entity);
            }
            target.addComponent<MeshRendererComponent>(meshId, "default_material");
            if (prim.material >= 0 && static_cast<size_t>(prim.material) < model.materials.size()) {
                target.addComponent<PBRMaterialComponent>(
                    buildMaterialFromGLTF(model, prim.material, baseDir));
            }
        }
    }

    for (int child : node.children) {
        addNodeEntity(scene, model, child, entity, baseDir, meshPrefix);
    }
}

Entity ModelImporter::importGLTF(Scene* scene, const std::string& filePath, const std::string& stem) {
    std::string resolved = resolveAssetPath(filePath);

    tinygltf::Model model;
    std::string err;
    if (!loadGLTFFile(resolved, model, err)) {
        std::cerr << "[ModelImporter] Failed to load glTF '" << filePath << "': " << err << "\n";
        return Entity();
    }

    // 解析一次，注册全部 primitive 到 MeshManager（后续渲染直接命中缓存）
    std::map<std::string, std::shared_ptr<Mesh>> meshes;
    collectGLTFMeshes(model, meshes);
    if (meshes.empty()) {
        std::cerr << "[ModelImporter] glTF contains no meshes: " << filePath << "\n";
        return Entity();
    }
    size_t registered = 0;
    for (const auto& [suffix, mesh] : meshes) {
        if (MeshManager::getInstance().registerMesh(resolved + suffix, mesh)) registered++;
    }

    std::string baseDir = fs::path(resolved).parent_path().string();
    Entity root = scene->createEntity(stem);

    const tinygltf::Scene* gltfScene = nullptr;
    if (!model.scenes.empty()) {
        int si = (model.defaultScene >= 0 && static_cast<size_t>(model.defaultScene) < model.scenes.size())
            ? model.defaultScene : 0;
        gltfScene = &model.scenes[si];
    }

    if (gltfScene) {
        for (int nodeIdx : gltfScene->nodes) {
            addNodeEntity(scene, model, nodeIdx, root, baseDir, resolved);
        }
    } else {
        // 无场景图的退化情况：按 mesh 平铺为根实体的子实体
        for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
            const auto& gltfMesh = model.meshes[mi];
            for (size_t pi = 0; pi < gltfMesh.primitives.size(); ++pi) {
                Entity e = scene->createEntity(
                    (gltfMesh.name.empty() ? "mesh_" + std::to_string(mi) : gltfMesh.name)
                    + "_" + std::to_string(pi));
                e.setParent(root);
                e.addComponent<MeshRendererComponent>(
                    resolved + "#" + std::to_string(mi) + "_" + std::to_string(pi), "default_material");
                if (gltfMesh.primitives[pi].material >= 0 &&
                    static_cast<size_t>(gltfMesh.primitives[pi].material) < model.materials.size()) {
                    e.addComponent<PBRMaterialComponent>(
                        buildMaterialFromGLTF(model, gltfMesh.primitives[pi].material, baseDir));
                }
            }
        }
    }

    std::cout << "[ModelImporter] Imported glTF '" << stem << "'"
              << " (meshes: " << registered << "/" << meshes.size() << ")\n";
    return root;
}

} // namespace VEngine
