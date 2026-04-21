#pragma once

/**
 * MeshClusterizer.h - 基于 UE5 Nanite 风格的网�?Cluster 化算�?
 * 
 * 实现思路参�?UE5 Nanite:
 * 1. 使用图分区算法（METIS 风格）将网格划分成均匀�?Cluster
 * 2. 每个 Cluster �?128 个三角形
 * 3. 通过边界优化减少 Cluster 间共享顶�?
 * 4. 支持递归简化生�?LOD DAG
 */

#include "NaniteCluster.h"
#include "Mesh.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace Nanite {

/**
 * 网格分割配置
 */
struct ClusterizerConfig {
    // 每个 Cluster 的目标三角形数量（UE5 默认 128�?
    uint32_t targetTrianglesPerCluster = CLUSTER_TARGET_TRIANGLES;
    
    // 每个 Cluster 的最大三角形数量
    uint32_t maxTrianglesPerCluster = CLUSTER_TARGET_TRIANGLES + 32;
    
    // 每个 Cluster 的最小三角形数量
    uint32_t minTrianglesPerCluster = 32;
    
    // 是否生成 LOD 层级
    bool generateLODs = true;
    
    // LOD 简化目标比例（每级保留的三角形比例）
    // 0.75 = 每级保留 75%，产生更多层级（7-8 层）
    float lodReductionRatio = 0.75f;
    
    // 最小 LOD 三角形数量（低于此值停止生成 LOD）
    uint32_t minLODTriangles = 8;
    
    // 是否压缩顶点数据
    bool packVertices = true;
    
    // 是否计算法线锥（用于背面剔除�?
    bool computeNormalCones = true;
    
    // ============ METIS 风格分区参数 ============
    
    // 图粗化时的最大匹配迭代次�?
    uint32_t coarseningIterations = 20;
    
    // 细化时的最大迭代次�?
    uint32_t refinementIterations = 10;
    
    // 分区不平衡容忍度�?.0 = 完全平衡�?
    float imbalanceTolerance = 1.05f;
    
    // 边界优化迭代次数
    uint32_t boundaryOptimizationIterations = 3;
};

/**
 * 输入的原始网格数�?
 */
struct InputMesh {
    // 顶点数据
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec4> tangents;
    
    // 索引数据（三角形列表�?
    std::vector<uint32_t> indices;
    
    // 网格名称（用于调试）
    std::string name;
    
    // 便捷方法
    uint32_t getTriangleCount() const { 
        return static_cast<uint32_t>(indices.size() / 3); 
    }
    
    uint32_t getVertexCount() const { 
        return static_cast<uint32_t>(positions.size()); 
    }
    
    uint32_t triangleCount() const {
        return getTriangleCount();
    }
    
    /**
     * �?Mesh 对象创建 InputMesh
     */
    static InputMesh fromMesh(const Mesh& mesh) {
        InputMesh result;
        result.name = mesh.getName();
        
        const auto& vertices = mesh.getVertices();
        const auto& srcIndices = mesh.getIndices();
        
        result.positions.reserve(vertices.size());
        result.normals.reserve(vertices.size());
        result.uvs.reserve(vertices.size());
        result.tangents.reserve(vertices.size());
        result.indices = srcIndices;
        
        for (const auto& v : vertices) {
            result.positions.push_back(v.pos);
            result.normals.push_back(v.normal);
            result.uvs.push_back(v.texCoord);
            result.tangents.push_back(glm::vec4(v.tangent, 1.0f));
        }
        
        return result;
    }
};

/**
 * MeshClusterizer - 基于 UE5 Nanite 风格的网�?Cluster 化处理器
 * 
 * 核心算法�?
 * 1. 构建三角形邻接图
 * 2. 使用多级图分区（�?METIS）进�?Cluster 划分
 * 3. 边界优化减少共享顶点
 * 4. 递归简化生�?LOD 层级
 */
class MeshClusterizer {
public:
    MeshClusterizer();
    ~MeshClusterizer() = default;
    
    void setConfig(const ClusterizerConfig& config) { m_config = config; }
    const ClusterizerConfig& getConfig() const { return m_config; }
    
    /**
     * 将网格分割成 Clusters（主入口�?
     */
    std::unique_ptr<ClusterizedMesh> clusterize(const InputMesh& inputMesh);
    
    /**
     * 生成 LOD 层级
     */
    void generateLODHierarchy(ClusterizedMesh& output);
    
    using ProgressCallback = std::function<void(float progress, const std::string& stage)>;
    void setProgressCallback(ProgressCallback callback) { m_progressCallback = callback; }

private:
    // ============== 图数据结�?==============
    
    /**
     * 三角形节点（图分区的基本单元�?
     */
    struct TriangleNode {
        uint32_t indices[3];        // 顶点索引
        glm::vec3 center;           // 中心�?
        glm::vec3 normal;           // 法线
        float area;                 // 面积（用于权重）
        
        uint32_t partitionId = ~0u; // 分区 ID（即 Cluster ID�?
        
        // 邻接信息
        std::vector<uint32_t> neighbors;       // 邻居三角形索�?
        std::vector<float> edgeWeights;        // 与邻居的边权�?
    };
    
    /**
     * 粗化图中的超节点（多个三角形合并�?
     */
    struct CoarseNode {
        std::vector<uint32_t> triangles;  // 包含的原始三角形
        glm::vec3 center;                  // 质心
        float totalArea;                   // 总面�?
        
        std::vector<uint32_t> neighbors;   // 粗化图中的邻�?
        std::vector<float> edgeWeights;    // 边权�?
        
        uint32_t partitionId = ~0u;
        uint32_t matchedWith = ~0u;        // 粗化时匹配的节点
    };
    
    /**
     * 边（用于邻接图）
     */
    struct Edge {
        uint32_t v0, v1;
        
        bool operator==(const Edge& other) const {
            return v0 == other.v0 && v1 == other.v1;
        }
        
        bool operator<(const Edge& other) const {
            if (v0 != other.v0) return v0 < other.v0;
            return v1 < other.v1;
        }
    };
    
    struct EdgeHash {
        size_t operator()(const Edge& e) const {
            return std::hash<uint64_t>()(
                (static_cast<uint64_t>(e.v0) << 32) | e.v1
            );
        }
    };
    
    // ============== 核心算法步骤 ==============
    
    /**
     * 步骤 1：构建三角形邻接�?
     */
    void buildTriangleGraph(const InputMesh& mesh);
    
    /**
     * 步骤 2：多级图分区（METIS 风格�?
     * - 粗化阶段：重复合并匹配的节点，直到足够小
     * - 初始分区：在最粗图上进行分�?
     * - 细化阶段：逐层映射回原图并优化
     */
    void multilevelPartition();
    
    /**
     * 步骤 3：边界优�?
     * 通过交换边界三角形来优化 Cluster 形状，减少共享顶�?
     */
    void optimizePartitionBoundaries();
    
    /**
     * 步骤 4：生�?Cluster 数据
     */
    void generateClusterData(const InputMesh& mesh, ClusterizedMesh& output);
    
    // ============== 粗化阶段辅助函数 ==============
    
    /**
     * 构建粗化图（合并匹配的节点）
     */
    void buildCoarseGraph(const std::vector<CoarseNode>& fineGraph, 
                          std::vector<CoarseNode>& coarseGraph);
    
    /**
     * 重边缘匹配（Heavy Edge Matching�?
     * 选择权重最大的边进行匹�?
     */
    void heavyEdgeMatching(std::vector<CoarseNode>& graph);
    
    // ============== 分区阶段辅助函数 ==============
    
    /**
     * 初始分区（在最粗图上使�?BFS 或贪心）
     */
    void initialPartition(std::vector<CoarseNode>& coarsestGraph, uint32_t numPartitions);
    
    /**
     * 将分区结果投影到细粒度图
     */
    void projectPartition(const std::vector<CoarseNode>& coarseGraph,
                          std::vector<CoarseNode>& fineGraph);
    
    // ============== 细化阶段辅助函数 ==============
    
    /**
     * KL/FM 风格的细�?
     * 尝试移动边界节点来改善分区质�?
     */
    void refinePartition(std::vector<CoarseNode>& graph);
    
    /**
     * 计算节点移动的增�?
     */
    float computeMoveGain(const std::vector<CoarseNode>& graph, 
                          uint32_t nodeIdx, 
                          uint32_t targetPartition);
    
    // ============== 边权重计�?==============
    
    /**
     * 计算两个三角形之间的边权�?
     * 考虑：共享边长度、法线相似度、面�?
     */
    float computeEdgeWeight(uint32_t tri1, uint32_t tri2, const InputMesh& mesh);
    
    /**
     * 计算三角形面�?
     */
    float computeTriangleArea(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2);
    
    // ============== 工具函数 ==============
    
    static Edge makeEdge(uint32_t v0, uint32_t v1) {
        return v0 < v1 ? Edge{v0, v1} : Edge{v1, v0};
    }
    
    uint32_t calculateTargetClusterCount() const;
    
    // ============== LOD 生成 ==============
    
    std::vector<std::vector<uint32_t>> groupClustersForLOD(
        const ClusterizedMesh& mesh,
        uint32_t startIndex,
        uint32_t count);
    
    std::unique_ptr<Cluster> simplifyClusterGroup(
        const ClusterizedMesh& mesh,
        const std::vector<uint32_t>& clusterIndices,
        uint32_t lodLevel);
    
    void updateClusterHierarchyInfo(ClusterizedMesh& output);
    
    // ============== 数据成员 ==============
    
    ClusterizerConfig m_config;
    ProgressCallback m_progressCallback;
    
    // 三角形图
    std::vector<TriangleNode> m_triangles;
    
    // �?-> 三角形映�?
    std::unordered_map<Edge, std::vector<uint32_t>, EdgeHash> m_edgeToTriangles;
    
    // 输入网格引用（用于边权重计算�?
    const InputMesh* m_inputMesh = nullptr;
    
    // 多级图层次（用于 METIS 风格分区�?
    std::vector<std::vector<CoarseNode>> m_graphHierarchy;
};

} // namespace Nanite
