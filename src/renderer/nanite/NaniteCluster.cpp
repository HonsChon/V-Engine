#include "NaniteCluster.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Nanite {

// ============== Cluster 实现 ==============

void Cluster::computeBounds() {
    if (vertices.empty()) return;
    
    // 计算 AABB
    bounds.aabbMin = glm::vec3(std::numeric_limits<float>::max());
    bounds.aabbMax = glm::vec3(std::numeric_limits<float>::lowest());
    
    for (const auto& vertex : vertices) {
        bounds.aabbMin = glm::min(bounds.aabbMin, vertex.position);
        bounds.aabbMax = glm::max(bounds.aabbMax, vertex.position);
    }
    
    // 计算包围�?
    bounds.center = (bounds.aabbMin + bounds.aabbMax) * 0.5f;
    bounds.radius = 0.0f;
    
    for (const auto& vertex : vertices) {
        float dist = glm::length(vertex.position - bounds.center);
        bounds.radius = std::max(bounds.radius, dist);
    }
    
    // 初始化其他字�?
    bounds.lodError = 0.0f;
    bounds.screenSizeThreshold = 0.0f;
}

void Cluster::computeNormalCone() {
    if (vertices.empty()) {
        bounds.coneAxis = glm::vec3(0.0f, 1.0f, 0.0f);
        bounds.coneAngleCos = -1.0f;  // 180度，表示无效
        return;
    }
    
    // 计算平均法线作为锥轴
    glm::vec3 avgNormal(0.0f);
    for (const auto& vertex : vertices) {
        avgNormal += vertex.normal;
    }
    
    if (glm::length(avgNormal) < 0.001f) {
        bounds.coneAxis = glm::vec3(0.0f, 1.0f, 0.0f);
        bounds.coneAngleCos = -1.0f;
        return;
    }
    
    bounds.coneAxis = glm::normalize(avgNormal);
    
    // 找到与锥轴夹角最大的法线
    float minCos = 1.0f;
    for (const auto& vertex : vertices) {
        glm::vec3 n = glm::normalize(vertex.normal);
        float cosAngle = glm::dot(n, bounds.coneAxis);
        minCos = std::min(minCos, cosAngle);
    }
    
    bounds.coneAngleCos = minCos;
}

void Cluster::packVertices() {
    packedVertices.resize(vertices.size());
    
    // 计算包围盒范围用于量�?
    glm::vec3 range = bounds.aabbMax - bounds.aabbMin;
    glm::vec3 invRange = glm::vec3(
        range.x > 0.0001f ? 1.0f / range.x : 0.0f,
        range.y > 0.0001f ? 1.0f / range.y : 0.0f,
        range.z > 0.0001f ? 1.0f / range.z : 0.0f
    );
    
    for (size_t i = 0; i < vertices.size(); ++i) {
        const Vertex& src = vertices[i];
        PackedVertex& dst = packedVertices[i];
        
        // 位置量化�?16 位（相对�?AABB�?
        glm::vec3 normalized = (src.position - bounds.aabbMin) * invRange;
        dst.posX = static_cast<uint16_t>(glm::clamp(normalized.x, 0.0f, 1.0f) * 65535.0f);
        dst.posY = static_cast<uint16_t>(glm::clamp(normalized.y, 0.0f, 1.0f) * 65535.0f);
        dst.posZ = static_cast<uint16_t>(glm::clamp(normalized.z, 0.0f, 1.0f) * 65535.0f);
        dst.padding0 = 0;
        
        // 法线使用八面体编�?
        // 简化版本：直接量化 XY 分量
        glm::vec3 n = glm::normalize(src.normal);
        dst.normalX = static_cast<int16_t>(n.x * 32767.0f);
        dst.normalY = static_cast<int16_t>(n.y * 32767.0f);
        
        // UV 量化�?16 �?
        dst.uvX = static_cast<uint16_t>(glm::clamp(src.uv.x, 0.0f, 1.0f) * 65535.0f);
        dst.uvY = static_cast<uint16_t>(glm::clamp(src.uv.y, 0.0f, 1.0f) * 65535.0f);
    }
}

// ============== ClusterizedMesh 实现 ==============

uint32_t ClusterizedMesh::getTotalTriangleCount() const {
    uint32_t total = 0;
    for (const auto& cluster : clusters) {
        total += cluster.triangleCount;
    }
    return total;
}

uint32_t ClusterizedMesh::getTotalClusterCount() const {
    return static_cast<uint32_t>(clusters.size());
}

uint32_t ClusterizedMesh::selectLODLevel(float screenSpaceError) const {
    // 从最精细�?LOD 开始检�?
    for (uint32_t lod = 0; lod < lodLevels.size(); ++lod) {
        // 如果�?LOD 的最大误差小于屏幕误差阈值，使用�?LOD
        if (lodLevels[lod].maxError <= screenSpaceError) {
            return lod;
        }
    }
    // 返回最粗糙�?LOD
    return static_cast<uint32_t>(lodLevels.size() - 1);
}

} // namespace Nanite
