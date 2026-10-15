/**
 * @file BVH.cpp
 * @brief SAH BVH 实现 — binned SAH 构建 + 显式栈遍历
 */

#include "BVH.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace VEngine {
namespace RT {

namespace {

/// AABB slab 测试：invDir = 1/dir（dir 允许非归一化）
inline bool aabbIntersect(
    const glm::vec3& min, const glm::vec3& max,
    const glm::vec3& orig, const glm::vec3& invDir,
    float tMax, float& tEnter)
{
    float t0 = 0.0f;
    float t1 = tMax;

    for (int i = 0; i < 3; ++i) {
        float ta = (min[i] - orig[i]) * invDir[i];
        float tb = (max[i] - orig[i]) * invDir[i];
        if (ta > tb) std::swap(ta, tb);
        t0 = ta > t0 ? ta : t0;
        t1 = tb < t1 ? tb : t1;
        if (t1 < t0) return false;
    }
    tEnter = t0;
    return true;
}

/// 半表面积 = xy+yz+zx（SAH 代价中的表面积项，常数因子可约去）
inline float halfArea(const glm::vec3& size) {
    return size.x * size.y + size.y * size.z + size.z * size.x;
}

} // anonymous namespace

// ============================================================
// 构建 — binned SAH
// ============================================================

void BVH::build(const RTTriangle* tris, uint32_t triCount) {
    tris_ = tris;
    triCount_ = triCount;
    nodes_.clear();
    triIndices_.clear();
    buildMs = 0.0f;
    nodeCountAlloc_ = 0;

    if (!tris_ || triCount_ == 0) return;

    auto startTime = std::chrono::high_resolution_clock::now();

    // 索引序列初始化 + 节点池预分配（二叉树最多 2n-1 个节点）
    triIndices_.resize(triCount_);
    for (uint32_t i = 0; i < triCount_; ++i) triIndices_[i] = i;
    nodes_.resize(static_cast<size_t>(triCount_) * 2);

    // 根节点
    nodes_[0].leftFirst = 0;
    nodes_[0].triCount = triCount_;
    updateNodeBounds(0, 0, triCount_);
    nodeCountAlloc_ = 1;

    subdivide(0, 0, triCount_, 0);

    nodes_.resize(nodeCountAlloc_);

    auto endTime = std::chrono::high_resolution_clock::now();
    buildMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
}

void BVH::updateNodeBounds(uint32_t nodeIdx, uint32_t first, uint32_t count) {
    BVHNode& node = nodes_[nodeIdx];
    node.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    node.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (uint32_t i = 0; i < count; ++i) {
        const RTTriangle& tri = tris_[triIndices_[first + i]];
        node.boundsMin = glm::min(node.boundsMin, tri.v0);
        node.boundsMax = glm::max(node.boundsMax, tri.v0);
        node.boundsMin = glm::min(node.boundsMin, tri.v1);
        node.boundsMax = glm::max(node.boundsMax, tri.v1);
        node.boundsMin = glm::min(node.boundsMin, tri.v2);
        node.boundsMax = glm::max(node.boundsMax, tri.v2);
    }
}

void BVH::subdivide(uint32_t nodeIdx, uint32_t first, uint32_t count, uint32_t depth) {
    BVHNode& node = nodes_[nodeIdx];

    // ---- binned SAH 寻最优分割 ----
    // 代价归一化：bin 面积除以父节点面积（无量纲），叶代价 = 三角数（求交代价 1/三角）
    const float parentHalfArea = std::max(halfArea(node.boundsMax - node.boundsMin), 1e-20f);
    const float invParentArea = 1.0f / parentHalfArea;
    const float leafCost = static_cast<float>(count);
    float bestCost = std::numeric_limits<float>::max();
    float bestSplit = 0.0f;
    int bestAxis = -1;

    // 质心范围
    glm::vec3 cMin(std::numeric_limits<float>::max());
    glm::vec3 cMax(std::numeric_limits<float>::lowest());
    for (uint32_t i = 0; i < count; ++i) {
        glm::vec3 c = tris_[triIndices_[first + i]].centroid();
        cMin = glm::min(cMin, c);
        cMax = glm::max(cMax, c);
    }

    if (depth < MAX_DEPTH && count > MAX_LEAF_TRIS) {
        for (int axis = 0; axis < 3; ++axis) {
            float extent = cMax[axis] - cMin[axis];
            if (extent < 1e-8f) continue;   // 该轴退化

            float scale = static_cast<float>(BINS) / extent;

            // bin 聚合（盒 + 计数）
            struct Bin {
                glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
                glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());
                uint32_t cnt = 0;
            };
            Bin bins[BINS];

            for (uint32_t i = 0; i < count; ++i) {
                const RTTriangle& tri = tris_[triIndices_[first + i]];
                glm::vec3 c = tri.centroid();
                uint32_t bin = std::min(BINS - 1,
                    static_cast<uint32_t>((c[axis] - cMin[axis]) * scale));
                Bin& b = bins[bin];
                b.cnt++;
                b.min = glm::min(b.min, tri.v0);
                b.max = glm::max(b.max, tri.v0);
                b.min = glm::min(b.min, tri.v1);
                b.max = glm::max(b.max, tri.v1);
                b.min = glm::min(b.min, tri.v2);
                b.max = glm::max(b.max, tri.v2);
            }

            // 前缀（左）/后缀（右）累积
            glm::vec3 lMin[BINS], lMax[BINS], rMin[BINS], rMax[BINS];
            uint32_t lCnt[BINS], rCnt[BINS];

            {
                glm::vec3 accMin(std::numeric_limits<float>::max());
                glm::vec3 accMax(std::numeric_limits<float>::lowest());
                uint32_t accCnt = 0;
                for (uint32_t b = 0; b < BINS; ++b) {
                    if (bins[b].cnt > 0) {
                        accMin = glm::min(accMin, bins[b].min);
                        accMax = glm::max(accMax, bins[b].max);
                    }
                    accCnt += bins[b].cnt;
                    lCnt[b] = accCnt; lMin[b] = accMin; lMax[b] = accMax;
                }
            }
            {
                glm::vec3 accMin(std::numeric_limits<float>::max());
                glm::vec3 accMax(std::numeric_limits<float>::lowest());
                uint32_t accCnt = 0;
                for (int b = BINS - 1; b >= 0; --b) {
                    if (bins[b].cnt > 0) {
                        accMin = glm::min(accMin, bins[b].min);
                        accMax = glm::max(accMax, bins[b].max);
                    }
                    accCnt += bins[b].cnt;
                    rCnt[b] = accCnt; rMin[b] = accMin; rMax[b] = accMax;
                }
            }

            // 扫描分割平面（bin b | b+1 之间），代价按父节点面积归一化
            for (uint32_t b = 0; b < BINS - 1; ++b) {
                if (lCnt[b] == 0 || rCnt[b + 1] == 0) continue;
                float cost = halfArea(lMax[b] - lMin[b]) * lCnt[b] * invParentArea
                           + halfArea(rMax[b + 1] - rMin[b + 1]) * rCnt[b + 1] * invParentArea;
                if (cost < bestCost) {
                    bestCost = cost;
                    bestAxis = axis;
                    bestSplit = cMin[axis] + (b + 1) / scale;
                }
            }
        }
    }

    // 无有效分割或代价劣于叶节点 → 叶节点（质心退化的大叶走索引中位数兜底）
    if (bestAxis < 0) {
        if (count > 4 * MAX_LEAF_TRIS && depth < MAX_DEPTH) {
            // 质心重合无法 SAH：按最长包围盒轴的中点二分（防退化超大叶）
            glm::vec3 size = node.boundsMax - node.boundsMin;
            bestAxis = 0;
            if (size.y > size[bestAxis]) bestAxis = 1;
            if (size.z > size[bestAxis]) bestAxis = 2;
            bestSplit = node.boundsMin[bestAxis] + size[bestAxis] * 0.5f;
        } else {
            node.leftFirst = first;
            node.triCount = count;
            return;
        }
    } else if (bestCost >= leafCost) {
        node.leftFirst = first;
        node.triCount = count;
        return;
    }

    // 按分割平面原地划分
    uint32_t mid = first;
    uint32_t end = first + count;
    while (mid < end) {
        glm::vec3 c = tris_[triIndices_[mid]].centroid();
        if (c[bestAxis] < bestSplit) {
            ++mid;
        } else {
            std::swap(triIndices_[mid], triIndices_[--end]);
        }
    }

    uint32_t leftCount = mid - first;
    // 退化保护：划分失败 → 叶节点
    if (leftCount == 0 || leftCount == count) {
        node.leftFirst = first;
        node.triCount = count;
        return;
    }

    // 内部节点：两个孩子
    node.triCount = 0;
    node.leftFirst = nodeCountAlloc_;
    uint32_t leftIdx = nodeCountAlloc_;
    nodeCountAlloc_ += 2;

    updateNodeBounds(leftIdx, first, leftCount);
    updateNodeBounds(leftIdx + 1, mid, count - leftCount);

    subdivide(leftIdx, first, leftCount, depth + 1);
    subdivide(leftIdx + 1, mid, count - leftCount, depth + 1);
}

// ============================================================
// 遍历 — 显式栈
// ============================================================

bool BVH::intersect(const glm::vec3& orig, const glm::vec3& dir, float tMax,
                    float& t, float& u, float& v, uint32_t& triIndex) const
{
    if (empty()) return false;

    glm::vec3 invDir(
        1.0f / (dir.x != 0.0f ? dir.x : 1e-30f),
        1.0f / (dir.y != 0.0f ? dir.y : 1e-30f),
        1.0f / (dir.z != 0.0f ? dir.z : 1e-30f));

    float bestT = tMax;
    bool hit = false;

    uint32_t stack[MAX_DEPTH * 2];
    int stackPtr = 0;
    stack[stackPtr++] = 0;

    while (stackPtr > 0) {
        uint32_t nodeIdx = stack[--stackPtr];
        const BVHNode& node = nodes_[nodeIdx];

        float tEnter;
        if (!aabbIntersect(node.boundsMin, node.boundsMax, orig, invDir, bestT, tEnter))
            continue;

        if (node.triCount > 0) {
            // 叶：遍历三角形，保留最近命中
            for (uint32_t i = 0; i < node.triCount; ++i) {
                uint32_t ti = triIndices_[node.leftFirst + i];
                float tt, uu, vv;
                if (intersectTriangle(orig, dir, tris_[ti], bestT, tt, uu, vv)) {
                    bestT = tt;
                    hit = true;
                    t = tt; u = uu; v = vv; triIndex = ti;
                }
            }
        } else {
            // 内部：孩子入栈（远先近后 → 近的先出栈）
            uint32_t left = node.leftFirst;
            uint32_t right = left + 1;
            float tL, tR;
            bool hitL = aabbIntersect(nodes_[left].boundsMin, nodes_[left].boundsMax,
                                      orig, invDir, bestT, tL);
            bool hitR = aabbIntersect(nodes_[right].boundsMin, nodes_[right].boundsMax,
                                      orig, invDir, bestT, tR);
            if (hitL && hitR) {
                // 远的先压栈
                if (tL < tR) {
                    stack[stackPtr++] = right;
                    stack[stackPtr++] = left;
                } else {
                    stack[stackPtr++] = left;
                    stack[stackPtr++] = right;
                }
            } else if (hitL) {
                stack[stackPtr++] = left;
            } else if (hitR) {
                stack[stackPtr++] = right;
            }
        }
    }

    return hit;
}

bool BVH::occluded(const glm::vec3& orig, const glm::vec3& dir, float tMax) const {
    if (empty()) return false;

    glm::vec3 invDir(
        1.0f / (dir.x != 0.0f ? dir.x : 1e-30f),
        1.0f / (dir.y != 0.0f ? dir.y : 1e-30f),
        1.0f / (dir.z != 0.0f ? dir.z : 1e-30f));

    uint32_t stack[MAX_DEPTH * 2];
    int stackPtr = 0;
    stack[stackPtr++] = 0;

    while (stackPtr > 0) {
        uint32_t nodeIdx = stack[--stackPtr];
        const BVHNode& node = nodes_[nodeIdx];

        float tEnter;
        if (!aabbIntersect(node.boundsMin, node.boundsMax, orig, invDir, tMax, tEnter))
            continue;

        if (node.triCount > 0) {
            for (uint32_t i = 0; i < node.triCount; ++i) {
                float tt, uu, vv;
                if (intersectTriangle(orig, dir, tris_[triIndices_[node.leftFirst + i]],
                                      tMax, tt, uu, vv)) {
                    return true;   // 阴影射线：任一命中即可
                }
            }
        } else {
            stack[stackPtr++] = node.leftFirst;
            stack[stackPtr++] = node.leftFirst + 1;
        }
    }
    return false;
}

} // namespace RT
} // namespace VEngine
