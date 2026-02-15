#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <vector>

namespace VulkanEngine {

/**
 * @brief 射线结构
 */
struct Ray {
    glm::vec3 origin;       // 射线起点
    glm::vec3 direction;    // 射线方向（归一化）

    Ray() : origin(0.0f), direction(0.0f, 0.0f, -1.0f) {}
    Ray(const glm::vec3& o, const glm::vec3& d) : origin(o), direction(glm::normalize(d)) {}

    /**
     * @brief 获取射线上的点
     * @param t 参数
     */
    glm::vec3 getPoint(float t) const {
        return origin + direction * t;
    }
};

/**
 * @brief 轴对齐包围盒 (AABB)
 */
struct AABB {
    glm::vec3 min;
    glm::vec3 max;

    AABB() : min(std::numeric_limits<float>::max()), max(std::numeric_limits<float>::lowest()) {}
    AABB(const glm::vec3& min, const glm::vec3& max) : min(min), max(max) {}

    /**
     * @brief 获取包围盒中心
     */
    glm::vec3 getCenter() const {
        return (min + max) * 0.5f;
    }

    /**
     * @brief 获取包围盒尺寸
     */
    glm::vec3 getSize() const {
        return max - min;
    }

    /**
     * @brief 扩展包围盒以包含点
     */
    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    /**
     * @brief 变换包围盒
     */
    AABB transform(const glm::mat4& matrix) const {
        // 获取8个顶点
        glm::vec3 corners[8] = {
            glm::vec3(min.x, min.y, min.z),
            glm::vec3(max.x, min.y, min.z),
            glm::vec3(min.x, max.y, min.z),
            glm::vec3(max.x, max.y, min.z),
            glm::vec3(min.x, min.y, max.z),
            glm::vec3(max.x, min.y, max.z),
            glm::vec3(min.x, max.y, max.z),
            glm::vec3(max.x, max.y, max.z)
        };

        AABB result;
        for (int i = 0; i < 8; i++) {
            glm::vec4 transformed = matrix * glm::vec4(corners[i], 1.0f);
            result.expand(glm::vec3(transformed));
        }
        return result;
    }
};

/**
 * @brief 射线拾取工具类
 */
class RayPicker {
public:
    /**
     * @brief 从屏幕坐标创建射线
     * @param screenX 屏幕 X 坐标
     * @param screenY 屏幕 Y 坐标
     * @param screenWidth 屏幕宽度
     * @param screenHeight 屏幕高度
     * @param viewMatrix 视图矩阵
     * @param projectionMatrix 投影矩阵
     * @return 世界空间中的射线
     */
    static Ray screenToWorldRay(
        float screenX, float screenY,
        float screenWidth, float screenHeight,
        const glm::mat4& viewMatrix,
        const glm::mat4& projectionMatrix)
    {
        // 1. 将屏幕坐标转换为 NDC (Normalized Device Coordinates)
        // 屏幕坐标原点在左上角，Y 向下
        // NDC 原点在中心，范围 [-1, 1]
        float ndcX = (2.0f * screenX) / screenWidth - 1.0f;
        float ndcY = 1.0f - (2.0f * screenY) / screenHeight;  // 翻转 Y

        // 2. 创建近裁剪面和远裁剪面上的点
        glm::vec4 nearPoint(ndcX, ndcY, -1.0f, 1.0f);  // z = -1 (近平面)
        glm::vec4 farPoint(ndcX, ndcY, 1.0f, 1.0f);    // z = 1 (远平面)

        // 3. 计算逆矩阵
        glm::mat4 invProjection = glm::inverse(projectionMatrix);
        glm::mat4 invView = glm::inverse(viewMatrix);

        // 4. 转换到世界空间
        // NDC -> View Space
        glm::vec4 nearView = invProjection * nearPoint;
        glm::vec4 farView = invProjection * farPoint;
        nearView /= nearView.w;  // 透视除法
        farView /= farView.w;

        // View Space -> World Space
        glm::vec4 nearWorld = invView * nearView;
        glm::vec4 farWorld = invView * farView;

        // 5. 创建射线
        glm::vec3 rayOrigin = glm::vec3(nearWorld);
        glm::vec3 rayDirection = glm::normalize(glm::vec3(farWorld) - glm::vec3(nearWorld));

        return Ray(rayOrigin, rayDirection);
    }

    /**
     * @brief 射线与 AABB 相交检测
     * @param ray 射线
     * @param aabb 包围盒
     * @param tMin 输出最小 t 值
     * @param tMax 输出最大 t 值
     * @return 是否相交
     */
    static bool rayIntersectsAABB(const Ray& ray, const AABB& aabb, float& tMin, float& tMax) {
        tMin = 0.0f;
        tMax = std::numeric_limits<float>::max();

        for (int i = 0; i < 3; i++) {
            float invD = 1.0f / ray.direction[i];
            float t0 = (aabb.min[i] - ray.origin[i]) * invD;
            float t1 = (aabb.max[i] - ray.origin[i]) * invD;

            if (invD < 0.0f) {
                std::swap(t0, t1);
            }

            tMin = t0 > tMin ? t0 : tMin;
            tMax = t1 < tMax ? t1 : tMax;

            if (tMax < tMin) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 射线与球体相交检测
     * @param ray 射线
     * @param center 球心
     * @param radius 半径
     * @param t 输出交点的 t 值
     * @return 是否相交
     */
    static bool rayIntersectsSphere(const Ray& ray, const glm::vec3& center, float radius, float& t) {
        glm::vec3 oc = ray.origin - center;
        float a = glm::dot(ray.direction, ray.direction);
        float b = 2.0f * glm::dot(oc, ray.direction);
        float c = glm::dot(oc, oc) - radius * radius;
        float discriminant = b * b - 4 * a * c;

        if (discriminant < 0) {
            return false;
        }

        float sqrtD = sqrt(discriminant);
        float t0 = (-b - sqrtD) / (2.0f * a);
        float t1 = (-b + sqrtD) / (2.0f * a);

        // 返回最近的正交点
        if (t0 > 0) {
            t = t0;
            return true;
        }
        if (t1 > 0) {
            t = t1;
            return true;
        }
        return false;
    }

    /**
     * @brief 射线与三角形相交检测 (Möller–Trumbore 算法)
     * @param ray 射线
     * @param v0, v1, v2 三角形顶点
     * @param t 输出交点的 t 值
     * @param u, v 输出重心坐标
     * @return 是否相交
     */
    static bool rayIntersectsTriangle(
        const Ray& ray,
        const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
        float& t, float& u, float& v)
    {
        const float EPSILON = 0.0000001f;
        
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 h = glm::cross(ray.direction, edge2);
        float a = glm::dot(edge1, h);

        if (a > -EPSILON && a < EPSILON) {
            return false;  // 射线与三角形平行
        }

        float f = 1.0f / a;
        glm::vec3 s = ray.origin - v0;
        u = f * glm::dot(s, h);

        if (u < 0.0f || u > 1.0f) {
            return false;
        }

        glm::vec3 q = glm::cross(s, edge1);
        v = f * glm::dot(ray.direction, q);

        if (v < 0.0f || u + v > 1.0f) {
            return false;
        }

        t = f * glm::dot(edge2, q);

        return t > EPSILON;
    }
};

} // namespace VulkanEngine
