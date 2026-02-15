#pragma once

#include <string>
#include <entt/entt.hpp>

// GLM 配置
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace VulkanEngine {

// 前向声明
class ScriptableEntity;

// ============================================================
// 核心组件 (Core Components)
// ============================================================

/**
 * @brief 标签组件 - 用于标识实体的名称
 */
struct TagComponent {
    std::string tag = "Entity";

    TagComponent() = default;
    TagComponent(const TagComponent&) = default;
    TagComponent(const std::string& tag) : tag(tag) {}
};

/**
 * @brief 变换组件 - 定义实体在3D空间中的位置、旋转和缩放
 */
struct TransformComponent {
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };  // 欧拉角 (弧度)
    glm::vec3 scale = { 1.0f, 1.0f, 1.0f };

    TransformComponent() = default;
    TransformComponent(const TransformComponent&) = default;
    TransformComponent(const glm::vec3& position) : position(position) {}

    /**
     * @brief 获取变换矩阵 (Model Matrix)
     */
    glm::mat4 getTransform() const {
        glm::mat4 rotationMatrix = glm::toMat4(glm::quat(rotation));
        
        return glm::translate(glm::mat4(1.0f), position)
            * rotationMatrix
            * glm::scale(glm::mat4(1.0f), scale);
    }

    /**
     * @brief 获取前向向量
     */
    glm::vec3 getForward() const {
        return glm::normalize(glm::vec3(
            cos(rotation.y) * cos(rotation.x),
            sin(rotation.x),
            sin(rotation.y) * cos(rotation.x)
        ));
    }

    /**
     * @brief 获取右向量
     */
    glm::vec3 getRight() const {
        return glm::normalize(glm::cross(getForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    /**
     * @brief 获取上向量
     */
    glm::vec3 getUp() const {
        return glm::normalize(glm::cross(getRight(), getForward()));
    }
};

/**
 * @brief 层级关系组件 - 定义父子关系
 */
struct RelationshipComponent {
    entt::entity parent = entt::null;
    entt::entity firstChild = entt::null;
    entt::entity nextSibling = entt::null;
    entt::entity prevSibling = entt::null;
    size_t childrenCount = 0;
};

// ============================================================
// 渲染组件 (Rendering Components)
// ============================================================

/**
 * @brief 网格渲染组件 - 引用要渲染的网格和材质
 */
struct MeshRendererComponent {
    std::string meshPath;           // 网格资源路径
    std::string materialPath;       // 材质资源路径
    bool castShadows = true;        // 是否投射阴影
    bool receiveShadows = true;     // 是否接收阴影
    bool visible = true;            // 是否可见

    MeshRendererComponent() = default;
    MeshRendererComponent(const std::string& mesh, const std::string& material = "")
        : meshPath(mesh), materialPath(material) {}
};

/**
 * @brief PBR 材质组件 - 物理材质属性
 */
struct PBRMaterialComponent {
    glm::vec3 albedo = { 1.0f, 1.0f, 1.0f };
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    glm::vec3 emissive = { 0.0f, 0.0f, 0.0f };
    float emissiveStrength = 0.0f;
    
    // 纹理路径
    std::string albedoMap;
    std::string normalMap;
    std::string metallicMap;
    std::string roughnessMap;
    std::string aoMap;
    std::string emissiveMap;
};

// ============================================================
// 光照组件 (Lighting Components)
// ============================================================

/**
 * @brief 光源类型枚举
 */
enum class LightType {
    Directional,    // 平行光
    Point,          // 点光源
    Spot            // 聚光灯
};

/**
 * @brief 光源组件
 */
struct LightComponent {
    LightType type = LightType::Point;
    glm::vec3 color = { 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    
    // 点光源/聚光灯属性
    float range = 10.0f;
    float constantAttenuation = 1.0f;
    float linearAttenuation = 0.09f;
    float quadraticAttenuation = 0.032f;
    
    // 聚光灯属性
    float innerConeAngle = glm::radians(12.5f);
    float outerConeAngle = glm::radians(17.5f);
    
    // 阴影属性
    bool castShadows = true;
    float shadowBias = 0.005f;
    int shadowMapResolution = 1024;

    LightComponent() = default;
    LightComponent(LightType type) : type(type) {}
};

// ============================================================
// 相机组件 (Camera Components)
// ============================================================

/**
 * @brief 相机投影类型
 */
enum class ProjectionType {
    Perspective,    // 透视投影
    Orthographic    // 正交投影
};

/**
 * @brief 相机组件
 */
struct CameraComponent {
    ProjectionType projectionType = ProjectionType::Perspective;
    bool isPrimary = false;         // 是否为主相机
    bool fixedAspectRatio = false;  // 是否固定宽高比
    
    // 透视投影参数
    float fov = glm::radians(45.0f);
    float aspectRatio = 16.0f / 9.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    
    // 正交投影参数
    float orthographicSize = 10.0f;
    float orthographicNear = -1.0f;
    float orthographicFar = 1.0f;

    /**
     * @brief 获取投影矩阵
     */
    glm::mat4 getProjection() const {
        if (projectionType == ProjectionType::Perspective) {
            return glm::perspective(fov, aspectRatio, nearClip, farClip);
        }
        else {
            float orthoLeft = -orthographicSize * aspectRatio * 0.5f;
            float orthoRight = orthographicSize * aspectRatio * 0.5f;
            float orthoBottom = -orthographicSize * 0.5f;
            float orthoTop = orthographicSize * 0.5f;
            
            return glm::ortho(orthoLeft, orthoRight, orthoBottom, orthoTop,
                             orthographicNear, orthographicFar);
        }
    }
};

// ============================================================
// 物理组件 (Physics Components) - 预留
// ============================================================

/**
 * @brief 刚体类型
 */
enum class RigidBodyType {
    Static,     // 静态物体
    Dynamic,    // 动态物体
    Kinematic   // 运动学物体
};

/**
 * @brief 刚体组件 - 预留给物理系统
 */
struct RigidBodyComponent {
    RigidBodyType type = RigidBodyType::Dynamic;
    float mass = 1.0f;
    float drag = 0.0f;
    float angularDrag = 0.05f;
    bool useGravity = true;
    bool isKinematic = false;
    
    // 运行时状态
    glm::vec3 velocity = { 0.0f, 0.0f, 0.0f };
    glm::vec3 angularVelocity = { 0.0f, 0.0f, 0.0f };
};

/**
 * @brief 盒形碰撞体组件
 */
struct BoxColliderComponent {
    glm::vec3 center = { 0.0f, 0.0f, 0.0f };
    glm::vec3 size = { 1.0f, 1.0f, 1.0f };
    bool isTrigger = false;
};

/**
 * @brief 球形碰撞体组件
 */
struct SphereColliderComponent {
    glm::vec3 center = { 0.0f, 0.0f, 0.0f };
    float radius = 0.5f;
    bool isTrigger = false;
};

// ============================================================
// 脚本组件 (Scripting Components) - 预留
// ============================================================

/**
 * @brief 原生脚本组件 - 用于绑定 C++ 脚本
 */
struct NativeScriptComponent {
    class ScriptableEntity* instance = nullptr;
    
    // 生命周期函数指针
    void (*instantiateScript)(NativeScriptComponent&) = nullptr;
    void (*destroyScript)(NativeScriptComponent&) = nullptr;
    
    template<typename T>
    void bind() {
        instantiateScript = [](NativeScriptComponent& comp) {
            comp.instance = new T();
        };
        destroyScript = [](NativeScriptComponent& comp) {
            delete comp.instance;
            comp.instance = nullptr;
        };
    }
};

// ============================================================
// 音频组件 (Audio Components) - 预留
// ============================================================

/**
 * @brief 音频源组件
 */
struct AudioSourceComponent {
    std::string audioClipPath;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool playOnAwake = false;
    bool spatialize = true;
    float minDistance = 1.0f;
    float maxDistance = 500.0f;
};

/**
 * @brief 音频监听器组件 (通常附加到主相机)
 */
struct AudioListenerComponent {
    bool active = true;
};

// ============================================================
// UUID 组件 - 全局唯一标识
// ============================================================

/**
 * @brief UUID 组件 - 用于序列化和场景持久化
 */
struct UUIDComponent {
    uint64_t uuid = 0;

    UUIDComponent() = default;
    UUIDComponent(uint64_t id) : uuid(id) {}
};

} // namespace VulkanEngine
