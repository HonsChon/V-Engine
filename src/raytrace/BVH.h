/**
 * @file BVH.h
 * @brief SAH BVH — 二叉包围盒层次加速结构（16-bin 表面积启发式构建）
 *
 * 节点扁平化存储（32 字节/节点，缓存友好），遍历用显式栈。
 * 建于网格局部空间三角形之上（RTTriangle），不拥有三角形数据。
 */

#pragma once

#include "RaytracerTypes.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace VEngine {
namespace RT {

/**
 * @brief BVH 节点（32 字节）
 * 叶节点:   leftFirst = 三角形索引区间起点（triIndices_ 内），triCount > 0
 * 内部节点: leftFirst = 左孩子节点索引，triCount = 0（右孩子 = leftFirst + 1）
 */
struct BVHNode {
    glm::vec3 boundsMin = glm::vec3(0.0f);
    uint32_t leftFirst = 0;
    glm::vec3 boundsMax = glm::vec3(0.0f);
    uint32_t triCount = 0;
};

class BVH {
public:
    BVH() = default;
    ~BVH() = default;

    // 禁止拷贝（tris_ 指向外部数据，拷贝语义易错）
    BVH(const BVH&) = delete;
    BVH& operator=(const BVH&) = delete;
    BVH(BVH&&) = default;
    BVH& operator=(BVH&&) = default;

    /**
     * @brief 构建 BVH
     * @param tris 三角形数组（调用方保证生命周期覆盖 BVH 使用期）
     * @param triCount 三角形数量
     */
    void build(const RTTriangle* tris, uint32_t triCount);

    /**
     * @brief 最近命中求交（遍历全部实例内三角形取最近）
     * @param orig/dir 局部空间射线（dir 不要求归一化，t 与 dir 长度比例一致）
     * @return 是否命中；输出 t/u/v 与三角形索引
     */
    bool intersect(const glm::vec3& orig, const glm::vec3& dir, float tMax,
                   float& t, float& u, float& v, uint32_t& triIndex) const;

    /**
     * @brief 遮挡测试（阴影射线）— 任一命中即返回，不需最近
     */
    bool occluded(const glm::vec3& orig, const glm::vec3& dir, float tMax) const;

    bool empty() const { return triCount_ == 0; }
    uint32_t nodeCount() const { return static_cast<uint32_t>(nodes_.size()); }
    uint32_t triangleCount() const { return triCount_; }

    /// 对象空间包围盒（根节点）
    glm::vec3 boundsMin() const { return nodes_.empty() ? glm::vec3(0.0f) : nodes_[0].boundsMin; }
    glm::vec3 boundsMax() const { return nodes_.empty() ? glm::vec3(0.0f) : nodes_[0].boundsMax; }

    /// 构建耗时统计（毫秒）
    float buildMs = 0.0f;

private:
    void subdivide(uint32_t nodeIdx, uint32_t first, uint32_t count, uint32_t depth);
    void updateNodeBounds(uint32_t nodeIdx, uint32_t first, uint32_t count);

    /// 展开内部递归深度上限（退化输入兜底，超限强制叶节点）
    static constexpr uint32_t MAX_DEPTH = 64;
    static constexpr uint32_t BINS = 16;
    static constexpr uint32_t MAX_LEAF_TRIS = 8;

    std::vector<BVHNode> nodes_;       // [0] 为根
    std::vector<uint32_t> triIndices_; // 重排后的三角形索引
    const RTTriangle* tris_ = nullptr; // 不拥有
    uint32_t triCount_ = 0;
    uint32_t nodeCountAlloc_ = 0;      // 构建期节点分配计数
};

} // namespace RT
} // namespace VEngine
