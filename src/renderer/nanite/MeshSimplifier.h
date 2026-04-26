#pragma once

/**
 * MeshSimplifier.h - QEM (Quadric Error Metrics) 网格简化算法
 * 
 * 基于 Michael Garland 和Paul Heckbert 的"Surface Simplification Using Quadric Error Metrics" (1997)
 * 
 * 核心思想：
 * 1. 为每个顶点计算误差二次曲面(Quadric)
 * 2. 为每条边计算折叠后的最优位置和误差
 * 3. 按误差从小到大折叠边，直到达到目标三角形数量
 * 
 * 这是 Nanite LOD 生成的核心算法：
 */

#include "NaniteCluster.h"
#include <glm/glm.hpp>
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>

namespace Nanite {

/**
 * 简化配置
 */
struct SimplifierConfig {
    // 目标三角形比例(0.0 - 1.0)
    float targetRatio = 0.5f;
    
    // 或者目标三角形数量 (0 表示使用 ratio)
    uint32_t targetTriangleCount = 0;
    
    // 最大几何误差(超过此值停止简化
    float maxError = FLT_MAX;
    
    // 是否保护边界顶(不折叠边界
    bool preserveBoundary = true;
    
    // 边界边的惩罚权重
    float boundaryPenalty = 100.0f;
    
    // 是否保护法线突变的边
    bool preserveNormalSeams = true;
    
    // 法线阈值(法线夹角超过此值认为是接缝)
    float normalSeamAngle = 30.0f;  // 度
    
    // 是否保护 UV 接缝
    bool preserveUVSeams = true;
    
    // UV 接缝惩罚权重
    float uvSeamPenalty = 10.0f;
    
    // 锁定的顶点索引集合（这些顶点不允许被折叠或移动）
    // 用于 Nanite Cluster Group 边界保护：
    // 确保相邻 Group 的共享边界顶点在简化后仍保持一致
    std::unordered_set<uint32_t> lockedVertices;
};

/**
 * 简化统计信息
 */
struct SimplificationStats {
    uint32_t originalVertices = 0;
    uint32_t originalTriangles = 0;
    uint32_t finalVertices = 0;
    uint32_t finalTriangles = 0;
    uint32_t edgeCollapses = 0;
    float maxGeometricError = 0.0f;
    float avgGeometricError = 0.0f;
    double timeMs = 0.0;
};

/**
 * 简化输出
 */
struct SimplifiedMesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    float geometricError = 0.0f;  // 简化引入的最大几何误差
    SimplificationStats stats;
};

/**
 * MeshSimplifier - QEM 网格简化器
 */
class MeshSimplifier {
public:
    MeshSimplifier();
    ~MeshSimplifier() = default;
    
    /**
     * 简化网格
     * @param vertices 输入顶点
     * @param indices 输入索引（三角形列表）
     * @param config 简化配置
     * @return 简化后的网格
     */
    std::unique_ptr<SimplifiedMesh> simplify(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        const SimplifierConfig& config
    );
    
    /**
     * 简化Cluster
     * @param cluster 输入 Cluster
     * @param targetTriangles 目标三角形数量
     * @return 简化后的Cluster
     */
    std::unique_ptr<Cluster> simplifyCluster(
        const Cluster& cluster,
        uint32_t targetTriangles
    );
    
    /**
     * 进度回调
     */
    using ProgressCallback = std::function<void(float progress)>;
    void setProgressCallback(ProgressCallback callback) { m_progressCallback = callback; }

private:
    // ============== 内部数据结构 ==============
    
    /**
     * Quadric - 误差二次曲面
     * 表示为4x4 对称矩阵的上三角部分 (10 个元素
     * Q = [a b c d]
     *     [b e f g]
     *     [c f h i]
     *     [d g i j]
     * 
     * 误差 = v^T * Q * v，其中v = [x, y, z, 1]
     */
    struct Quadric {
        double a, b, c, d;
        double    e, f, g;
        double       h, i;
        double          j;
        
        Quadric() : a(0), b(0), c(0), d(0), e(0), f(0), g(0), h(0), i(0), j(0) {}
        
        // 从平面方程创建Quadric
        // 平面: ax + by + cz + d = 0 (其中 a^2 + b^2 + c^2 = 1)
        static Quadric fromPlane(double px, double py, double pz, double pd) {
            Quadric q;
            q.a = px * px;
            q.b = px * py;
            q.c = px * pz;
            q.d = px * pd;
            q.e = py * py;
            q.f = py * pz;
            q.g = py * pd;
            q.h = pz * pz;
            q.i = pz * pd;
            q.j = pd * pd;
            return q;
        }
        
        // 加法
        Quadric operator+(const Quadric& other) const {
            Quadric result;
            result.a = a + other.a;
            result.b = b + other.b;
            result.c = c + other.c;
            result.d = d + other.d;
            result.e = e + other.e;
            result.f = f + other.f;
            result.g = g + other.g;
            result.h = h + other.h;
            result.i = i + other.i;
            result.j = j + other.j;
            return result;
        }
        
        Quadric& operator+=(const Quadric& other) {
            a += other.a; b += other.b; c += other.c; d += other.d;
            e += other.e; f += other.f; g += other.g;
            h += other.h; i += other.i;
            j += other.j;
            return *this;
        }
        
        // 标量乘法
        Quadric operator*(double scalar) const {
            Quadric result;
            result.a = a * scalar;
            result.b = b * scalar;
            result.c = c * scalar;
            result.d = d * scalar;
            result.e = e * scalar;
            result.f = f * scalar;
            result.g = g * scalar;
            result.h = h * scalar;
            result.i = i * scalar;
            result.j = j * scalar;
            return result;
        }
        
        // 计算点的误差: v^T * Q * v
        double evaluate(const glm::dvec3& v) const {
            return a * v.x * v.x + 2 * b * v.x * v.y + 2 * c * v.x * v.z + 2 * d * v.x
                 + e * v.y * v.y + 2 * f * v.y * v.z + 2 * g * v.y
                 + h * v.z * v.z + 2 * i * v.z
                 + j;
        }
        
        // 计算最优顶点位置(最小化误差)
        // 返回 false 如果矩阵奇异
        bool computeOptimalPosition(glm::dvec3& outPos) const;
    };
    
    /**
     * 内部顶点表示
     */
    struct InternalVertex {
        glm::dvec3 position;
        glm::dvec3 normal;
        glm::dvec2 uv;
        glm::dvec4 tangent;
        
        Quadric quadric;
        std::vector<uint32_t> adjacentFaces;  // 邻接三角形
        std::vector<uint32_t> adjacentEdges;  // 邻接图
        
        bool isValid = true;      // 是否有效（未被删除）
        bool isBoundary = false;  // 是否是边界顶点
        uint32_t remappedIndex = ~0u;  // 输出时的重映射索引
    };
    
    /**
     * 内部三角形表示
     */
    struct InternalFace {
        uint32_t v[3];  // 顶点索引
        glm::dvec3 normal;
        bool isValid = true;
    };
    
    /**
     * 边折叠候选
     */
    struct EdgeCollapse {
        uint32_t edgeIndex;
        uint32_t v0, v1;          // 端点 (v0 将被保留, v1 将被删除)
        glm::dvec3 optimalPos;    // 折叠后的最优位置
        double error;             // 折叠误差
        
        // 优先队列比较（误差小的优先）
        bool operator>(const EdgeCollapse& other) const {
            return error > other.error;
        }
    };
    
    /**
     * 边
     */
    struct InternalEdge {
        uint32_t v0, v1;
        std::vector<uint32_t> adjacentFaces;
        bool isValid = true;
        bool isBoundary = false;
        uint32_t heapIndex = ~0u;  // 在堆中的位置（用于更新）
    };
    
    // ============== 主要算法步骤 ==============
    
    void initialize(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void buildAdjacency();
    void computeInitialQuadrics();
    void identifyBoundaryEdges();
    void initializeCollapseQueue();
    void performSimplification(const SimplifierConfig& config);
    std::unique_ptr<SimplifiedMesh> buildOutput();
    
    // ============== 辅助函数 ==============
    
    // 计算三角形的平面方程
    void computeFacePlane(uint32_t faceIndex, double& a, double& b, double& c, double& d);
    
    // 计算边折叠的误差和最优位置
    EdgeCollapse computeEdgeCollapse(uint32_t edgeIndex);
    
    // 执行边折叠
    void collapseEdge(const EdgeCollapse& collapse);
    
    // 更新折叠后受影响的边
    void updateAffectedEdges(uint32_t vertexIndex);
    
    // 检查边折叠是否会导致拓扑问题（如翻转三角形）
    bool isCollapseValid(const EdgeCollapse& collapse);
    
    // 获取或创建边索引
    uint32_t getOrCreateEdge(uint32_t v0, uint32_t v1);
    
    // 创建边的键
    static uint64_t makeEdgeKey(uint32_t v0, uint32_t v1) {
        if (v0 > v1) std::swap(v0, v1);
        return (static_cast<uint64_t>(v0) << 32) | v1;
    }
    
    // ============== 数据成员 ==============
    
    std::vector<InternalVertex> m_vertices;
    std::vector<InternalFace> m_faces;
    std::vector<InternalEdge> m_edges;
    
    // 边键到索引的映射
    std::unordered_map<uint64_t, uint32_t> m_edgeMap;
    
    // 锁定的顶点集合（不允许折叠或移动）
    std::unordered_set<uint32_t> m_lockedVertices;
    
    // 边折叠优先队列
    std::priority_queue<EdgeCollapse, std::vector<EdgeCollapse>, std::greater<EdgeCollapse>> m_collapseQueue;
    
    // 统计
    uint32_t m_currentTriangleCount = 0;
    uint32_t m_currentVertexCount = 0;
    float m_maxError = 0.0f;
    double m_totalError = 0.0;
    uint32_t m_collapseCount = 0;
    
    // 回调
    ProgressCallback m_progressCallback;
};

} // namespace Nanite
