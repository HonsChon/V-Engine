#include "MeshClusterizer.h"
#include "MeshSimplifier.h"
#include <algorithm>
#include <queue>
#include <cmath>
#include <iostream>
#include <random>
#include <numeric>

namespace Nanite {

MeshClusterizer::MeshClusterizer() {
    // 使用默认配置
}

std::unique_ptr<ClusterizedMesh> MeshClusterizer::clusterize(const InputMesh& inputMesh) {
    auto result = std::make_unique<ClusterizedMesh>();
    
    // 记录原始网格信息
    result->originalTriangleCount = inputMesh.getTriangleCount();
    result->originalVertexCount = inputMesh.getVertexCount();
    result->sourceMeshName = inputMesh.name;
    
    if (inputMesh.indices.empty() || inputMesh.positions.empty()) {
        return result;
    }
    
    m_inputMesh = &inputMesh;
    
    // 步骤 1：构建三角形邻接�?
    if (m_progressCallback) {
        m_progressCallback(0.1f, "Building triangle adjacency graph");
    }
    buildTriangleGraph(inputMesh);
    
    std::cout << "[MeshClusterizer] Built graph with " << m_triangles.size() 
              << " triangles" << std::endl;
    
    // 步骤 2：多级图分区（METIS 风格�?
    if (m_progressCallback) {
        m_progressCallback(0.3f, "Multilevel graph partitioning");
    }
    multilevelPartition();
    
    // 步骤 3：边界优�?
    if (m_progressCallback) {
        m_progressCallback(0.5f, "Optimizing partition boundaries");
    }
    optimizePartitionBoundaries();
    
    // 步骤 4：生�?Cluster 数据
    if (m_progressCallback) {
        m_progressCallback(0.7f, "Generating cluster data");
    }
    generateClusterData(inputMesh, *result);
    
    // 设置 LOD 0 信息
    ClusterizedMesh::LODLevel lod0;
    lod0.clusterStartIndex = 0;
    lod0.clusterCount = static_cast<uint32_t>(result->clusters.size());
    lod0.maxError = 0.0f;
    result->lodLevels.push_back(lod0);
    
    // 步骤 5：生�?LOD 层级
    if (m_config.generateLODs && result->clusters.size() > 4) {
        if (m_progressCallback) {
            m_progressCallback(0.85f, "Generating LOD hierarchy");
        }
        generateLODHierarchy(*result);
    }
    
    if (m_progressCallback) {
        m_progressCallback(1.0f, "Complete");
    }
    
    // 清理
    m_triangles.clear();
    m_edgeToTriangles.clear();
    m_graphHierarchy.clear();
    m_inputMesh = nullptr;
    
    return result;
}

// ============================================================================
// 步骤 1：构建三角形邻接�?
// ============================================================================

void MeshClusterizer::buildTriangleGraph(const InputMesh& mesh) {
    uint32_t triangleCount = mesh.getTriangleCount();
    m_edgeToTriangles.clear();
    
    // ============ 预处理：仅移除退化三角形（面积为0） ============
    // 不再做"重复"三角形检测 —— 之前的中心+法线量化去重
    // 会误删大量有效三角形（尤其是高曲率表面上相邻的小三角形），
    // 导致 LOD 0 就出现缺口
    std::vector<uint32_t> validTriangleIndices;
    validTriangleIndices.reserve(triangleCount);
    
    uint32_t degenerateCount = 0;
    
    for (uint32_t i = 0; i < triangleCount; ++i) {
        uint32_t idx0 = mesh.indices[i * 3 + 0];
        uint32_t idx1 = mesh.indices[i * 3 + 1];
        uint32_t idx2 = mesh.indices[i * 3 + 2];
        
        // 跳过索引相同的退化三角形
        if (idx0 == idx1 || idx1 == idx2 || idx0 == idx2) {
            degenerateCount++;
            continue;
        }
        
        const glm::vec3& v0 = mesh.positions[idx0];
        const glm::vec3& v1 = mesh.positions[idx1];
        const glm::vec3& v2 = mesh.positions[idx2];
        
        // 检查面积退化三角形
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 cross = glm::cross(edge1, edge2);
        float crossLen = glm::length(cross);
        
        if (crossLen < 1e-8f) {
            degenerateCount++;
            continue;
        }
        
        validTriangleIndices.push_back(i);
    }
    
    if (degenerateCount > 0) {
        std::cout << "[MeshClusterizer] Pre-processing: removed " << degenerateCount 
                  << " degenerate triangles" << std::endl;
        std::cout << "[MeshClusterizer] Valid triangles: " << validTriangleIndices.size() 
                  << " / " << triangleCount << std::endl;
    }
    
    // ============ 构建三角形图（仅使用有效三角形）============
    // 创建原始索引到新索引的映�?
    std::vector<uint32_t> originalToNewIndex(triangleCount, ~0u);
    m_triangles.resize(validTriangleIndices.size());
    
    // 第一遍：创建三角形节点，建立�?-> 三角形映�?
    for (uint32_t newIdx = 0; newIdx < validTriangleIndices.size(); ++newIdx) {
        uint32_t originalIdx = validTriangleIndices[newIdx];
        originalToNewIndex[originalIdx] = newIdx;
        
        TriangleNode& tri = m_triangles[newIdx];
        
        tri.indices[0] = mesh.indices[originalIdx * 3 + 0];
        tri.indices[1] = mesh.indices[originalIdx * 3 + 1];
        tri.indices[2] = mesh.indices[originalIdx * 3 + 2];
        
        const glm::vec3& v0 = mesh.positions[tri.indices[0]];
        const glm::vec3& v1 = mesh.positions[tri.indices[1]];
        const glm::vec3& v2 = mesh.positions[tri.indices[2]];
        
        // 计算中心
        tri.center = (v0 + v1 + v2) / 3.0f;
        
        // 计算法线
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 cross = glm::cross(edge1, edge2);
        float crossLen = glm::length(cross);
        tri.normal = crossLen > 1e-8f ? cross / crossLen : glm::vec3(0, 1, 0);
        
        // 计算面积
        tri.area = crossLen * 0.5f;
        
        tri.partitionId = ~0u;
        
        // 建立边映射（使用新索引）
        Edge e0 = makeEdge(tri.indices[0], tri.indices[1]);
        Edge e1 = makeEdge(tri.indices[1], tri.indices[2]);
        Edge e2 = makeEdge(tri.indices[2], tri.indices[0]);
        
        m_edgeToTriangles[e0].push_back(newIdx);
        m_edgeToTriangles[e1].push_back(newIdx);
        m_edgeToTriangles[e2].push_back(newIdx);
    }
    
    // 第二遍：建立三角形邻接关系和边权�?
    uint32_t validTriCount = static_cast<uint32_t>(m_triangles.size());
    for (uint32_t i = 0; i < validTriCount; ++i) {
        TriangleNode& tri = m_triangles[i];
        
        std::set<uint32_t> neighborSet;  // 去重
        
        Edge edges[3] = {
            makeEdge(tri.indices[0], tri.indices[1]),
            makeEdge(tri.indices[1], tri.indices[2]),
            makeEdge(tri.indices[2], tri.indices[0])
        };
        
        for (const Edge& edge : edges) {
            auto it = m_edgeToTriangles.find(edge);
            if (it != m_edgeToTriangles.end()) {
                for (uint32_t neighborIdx : it->second) {
                    if (neighborIdx != i) {
                        neighborSet.insert(neighborIdx);
                    }
                }
            }
        }
        
        // 计算边权�?
        tri.neighbors.reserve(neighborSet.size());
        tri.edgeWeights.reserve(neighborSet.size());
        
        for (uint32_t neighborIdx : neighborSet) {
            tri.neighbors.push_back(neighborIdx);
            float weight = computeEdgeWeight(i, neighborIdx, mesh);
            tri.edgeWeights.push_back(weight);
        }
    }
}

float MeshClusterizer::computeEdgeWeight(uint32_t tri1, uint32_t tri2, const InputMesh& mesh) {
    const TriangleNode& t1 = m_triangles[tri1];
    const TriangleNode& t2 = m_triangles[tri2];
    
    // 找到共享�?
    std::vector<uint32_t> sharedVerts;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (t1.indices[i] == t2.indices[j]) {
                sharedVerts.push_back(t1.indices[i]);
            }
        }
    }
    
    float weight = 1.0f;
    
    // 共享边长度（边越长，权重越高，倾向于保持在同一 Cluster�?
    if (sharedVerts.size() >= 2) {
        const glm::vec3& v0 = mesh.positions[sharedVerts[0]];
        const glm::vec3& v1 = mesh.positions[sharedVerts[1]];
        float edgeLength = glm::length(v1 - v0);
        weight *= (1.0f + edgeLength);
    }
    
    // 法线相似度（法线越相似，权重越高�?
    float normalDot = glm::dot(t1.normal, t2.normal);
    float normalWeight = (normalDot + 1.0f) * 0.5f;  // 映射�?[0, 1]
    normalWeight = normalWeight * normalWeight;       // 加强差异
    weight *= (0.5f + normalWeight);
    
    // 面积因子（较大的三角形边权重更高�?
    float areaFactor = std::sqrt(t1.area * t2.area);
    weight *= (1.0f + areaFactor * 0.1f);
    
    return weight;
}

float MeshClusterizer::computeTriangleArea(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) {
    glm::vec3 cross = glm::cross(v1 - v0, v2 - v0);
    return glm::length(cross) * 0.5f;
}

uint32_t MeshClusterizer::calculateTargetClusterCount() const {
    uint32_t triCount = static_cast<uint32_t>(m_triangles.size());
    uint32_t targetClusters = (triCount + m_config.targetTrianglesPerCluster - 1) 
                              / m_config.targetTrianglesPerCluster;
    return std::max(targetClusters, 1u);
}

// ============================================================================
// 步骤 2：多级图分区（METIS 风格�?
// ============================================================================

void MeshClusterizer::multilevelPartition() {
    uint32_t targetClusters = calculateTargetClusterCount();
    
    if (targetClusters <= 1) {
        // 所有三角形放入一�?Cluster
        for (auto& tri : m_triangles) {
            tri.partitionId = 0;
        }
        std::cout << "[MeshClusterizer] Single cluster (small mesh)" << std::endl;
        return;
    }
    
    std::cout << "[MeshClusterizer] Target cluster count: " << targetClusters << std::endl;
    
    // 初始化第一层（原始三角形图�?
    m_graphHierarchy.clear();
    m_graphHierarchy.emplace_back();
    auto& baseGraph = m_graphHierarchy[0];
    baseGraph.resize(m_triangles.size());
    
    for (size_t i = 0; i < m_triangles.size(); ++i) {
        CoarseNode& node = baseGraph[i];
        node.triangles.push_back(static_cast<uint32_t>(i));
        node.center = m_triangles[i].center;
        node.totalArea = m_triangles[i].area;
        node.neighbors = m_triangles[i].neighbors;
        node.edgeWeights = m_triangles[i].edgeWeights;
        node.partitionId = ~0u;
        node.matchedWith = ~0u;
    }
    
    // ========== 粗化阶段 ==========
    // 重复合并节点直到图足够小
    uint32_t level = 0;
    uint32_t minNodes = targetClusters * 2;  // 最粗图至少�?2 * k 个节�?
    
    while (m_graphHierarchy[level].size() > minNodes && 
           level < m_config.coarseningIterations) {
        
        auto& currentGraph = m_graphHierarchy[level];
        
        // 重边缘匹�?
        heavyEdgeMatching(currentGraph);
        
        // 构建更粗的图
        m_graphHierarchy.emplace_back();
        buildCoarseGraph(currentGraph, m_graphHierarchy[level + 1]);
        
        // 检查是否有足够的收�?
        if (m_graphHierarchy[level + 1].size() >= currentGraph.size() * 0.9f) {
            // 收缩不足，停�?
            m_graphHierarchy.pop_back();
            break;
        }
        
        std::cout << "[MeshClusterizer] Coarsening level " << level + 1 
                  << ": " << m_graphHierarchy[level + 1].size() << " nodes" << std::endl;
        
        level++;
    }
    
    // ========== 初始分区阶段 ==========
    // 在最粗图上进行分�?
    auto& coarsestGraph = m_graphHierarchy.back();
    initialPartition(coarsestGraph, targetClusters);
    
    std::cout << "[MeshClusterizer] Initial partition on " << coarsestGraph.size() 
              << " coarse nodes" << std::endl;
    
    // ========== 细化阶段 ==========
    // 从粗到细，投影分区并进行局部优�?
    for (int l = static_cast<int>(m_graphHierarchy.size()) - 2; l >= 0; --l) {
        // 投影分区结果
        projectPartition(m_graphHierarchy[l + 1], m_graphHierarchy[l]);
        
        // 局部细�?
        refinePartition(m_graphHierarchy[l]);
    }
    
    // 将分区结果写回原始三角形
    for (size_t i = 0; i < m_graphHierarchy[0].size(); ++i) {
        const CoarseNode& node = m_graphHierarchy[0][i];
        for (uint32_t triIdx : node.triangles) {
            m_triangles[triIdx].partitionId = node.partitionId;
        }
    }
    
    // 统计分区结果
    std::unordered_map<uint32_t, uint32_t> partitionSizes;
    for (const auto& tri : m_triangles) {
        partitionSizes[tri.partitionId]++;
    }
    std::cout << "[MeshClusterizer] Created " << partitionSizes.size() << " partitions" << std::endl;
}

void MeshClusterizer::heavyEdgeMatching(std::vector<CoarseNode>& graph) {
    // 重置匹配状�?
    for (auto& node : graph) {
        node.matchedWith = ~0u;
    }
    
    // 随机排列节点访问顺序（避免顺序偏差）
    std::vector<uint32_t> nodeOrder(graph.size());
    std::iota(nodeOrder.begin(), nodeOrder.end(), 0);
    
    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(nodeOrder.begin(), nodeOrder.end(), rng);
    
    // 贪心匹配：每个未匹配节点选择权重最大的未匹配邻�?
    for (uint32_t nodeIdx : nodeOrder) {
        CoarseNode& node = graph[nodeIdx];
        
        if (node.matchedWith != ~0u) continue;  // 已匹�?
        
        uint32_t bestNeighbor = ~0u;
        float bestWeight = -1.0f;
        
        for (size_t j = 0; j < node.neighbors.size(); ++j) {
            uint32_t neighborIdx = node.neighbors[j];
            const CoarseNode& neighbor = graph[neighborIdx];
            
            if (neighbor.matchedWith == ~0u) {  // 邻居未匹�?
                float weight = node.edgeWeights[j];
                if (weight > bestWeight) {
                    bestWeight = weight;
                    bestNeighbor = neighborIdx;
                }
            }
        }
        
        if (bestNeighbor != ~0u) {
            node.matchedWith = bestNeighbor;
            graph[bestNeighbor].matchedWith = nodeIdx;
        }
    }
}

void MeshClusterizer::buildCoarseGraph(const std::vector<CoarseNode>& fineGraph,
                                        std::vector<CoarseNode>& coarseGraph) {
    coarseGraph.clear();
    
    // 为每个细粒度节点分配粗粒度索�?
    std::vector<uint32_t> fineToCoarse(fineGraph.size(), ~0u);
    
    for (size_t i = 0; i < fineGraph.size(); ++i) {
        const CoarseNode& node = fineGraph[i];
        
        if (fineToCoarse[i] != ~0u) continue;  // 已处�?
        
        uint32_t coarseIdx = static_cast<uint32_t>(coarseGraph.size());
        coarseGraph.emplace_back();
        CoarseNode& coarseNode = coarseGraph.back();
        
        // 添加当前节点的三角形
        coarseNode.triangles = node.triangles;
        coarseNode.center = node.center * node.totalArea;
        coarseNode.totalArea = node.totalArea;
        fineToCoarse[i] = coarseIdx;
        
        // 如果有匹配，合并匹配节点
        if (node.matchedWith != ~0u && node.matchedWith < fineGraph.size()) {
            const CoarseNode& matched = fineGraph[node.matchedWith];
            
            coarseNode.triangles.insert(coarseNode.triangles.end(),
                                        matched.triangles.begin(),
                                        matched.triangles.end());
            coarseNode.center += matched.center * matched.totalArea;
            coarseNode.totalArea += matched.totalArea;
            fineToCoarse[node.matchedWith] = coarseIdx;
        }
        
        // 归一化中�?
        if (coarseNode.totalArea > 0) {
            coarseNode.center /= coarseNode.totalArea;
        }
    }
    
    // 构建粗化图的邻接关系
    for (uint32_t ci = 0; ci < coarseGraph.size(); ++ci) {
        CoarseNode& coarseNode = coarseGraph[ci];
        std::unordered_map<uint32_t, float> neighborWeights;
        
        // 收集所有细粒度节点的邻�?
        for (size_t fi = 0; fi < fineGraph.size(); ++fi) {
            if (fineToCoarse[fi] != ci) continue;
            
            const CoarseNode& fineNode = fineGraph[fi];
            for (size_t j = 0; j < fineNode.neighbors.size(); ++j) {
                uint32_t fineNeighbor = fineNode.neighbors[j];
                uint32_t coarseNeighbor = fineToCoarse[fineNeighbor];
                
                if (coarseNeighbor != ci && coarseNeighbor != ~0u) {
                    neighborWeights[coarseNeighbor] += fineNode.edgeWeights[j];
                }
            }
        }
        
        // 转换为邻接列�?
        for (const auto& [neighbor, weight] : neighborWeights) {
            coarseNode.neighbors.push_back(neighbor);
            coarseNode.edgeWeights.push_back(weight);
        }
    }
}

void MeshClusterizer::initialPartition(std::vector<CoarseNode>& coarsestGraph, 
                                        uint32_t numPartitions) {
    if (coarsestGraph.empty()) return;
    
    // 使用 BFS 贪心分区
    // 选择多个种子点，然后交替扩展
    
    uint32_t numNodes = static_cast<uint32_t>(coarsestGraph.size());
    numPartitions = std::min(numPartitions, numNodes);
    
    // 初始�?
    for (auto& node : coarsestGraph) {
        node.partitionId = ~0u;
    }
    
    // 选择种子点（尽量分散�?
    std::vector<uint32_t> seeds;
    std::vector<bool> selected(numNodes, false);
    
    // 第一个种子：随机选择
    std::random_device rd;
    std::mt19937 rng(rd());
    seeds.push_back(rng() % numNodes);
    selected[seeds[0]] = true;
    coarsestGraph[seeds[0]].partitionId = 0;
    
    // 后续种子：选择距离已有种子最远的�?
    for (uint32_t p = 1; p < numPartitions; ++p) {
        uint32_t bestNode = 0;
        float maxMinDist = -1.0f;
        
        for (uint32_t i = 0; i < numNodes; ++i) {
            if (selected[i]) continue;
            
            // 计算到所有已选种子的最小距�?
            float minDist = std::numeric_limits<float>::max();
            for (uint32_t seed : seeds) {
                float dist = glm::length(coarsestGraph[i].center - coarsestGraph[seed].center);
                minDist = std::min(minDist, dist);
            }
            
            if (minDist > maxMinDist) {
                maxMinDist = minDist;
                bestNode = i;
            }
        }
        
        seeds.push_back(bestNode);
        selected[bestNode] = true;
        coarsestGraph[bestNode].partitionId = p;
    }
    
    // BFS 扩展：交替从每个分区扩展
    std::vector<std::queue<uint32_t>> queues(numPartitions);
    std::vector<float> partitionSizes(numPartitions, 0.0f);
    
    for (uint32_t p = 0; p < numPartitions; ++p) {
        queues[p].push(seeds[p]);
        partitionSizes[p] = coarsestGraph[seeds[p]].totalArea;
    }
    
    float targetSize = 0.0f;
    for (const auto& node : coarsestGraph) {
        targetSize += node.totalArea;
    }
    targetSize /= numPartitions;
    float maxSize = targetSize * m_config.imbalanceTolerance;
    
    bool progress = true;
    while (progress) {
        progress = false;
        
        for (uint32_t p = 0; p < numPartitions; ++p) {
            if (queues[p].empty()) continue;
            if (partitionSizes[p] >= maxSize) continue;
            
            uint32_t current = queues[p].front();
            queues[p].pop();
            
            // 按边权重排序邻居
            std::vector<std::pair<float, uint32_t>> neighbors;
            for (size_t j = 0; j < coarsestGraph[current].neighbors.size(); ++j) {
                uint32_t neighborIdx = coarsestGraph[current].neighbors[j];
                if (coarsestGraph[neighborIdx].partitionId == ~0u) {
                    neighbors.emplace_back(coarsestGraph[current].edgeWeights[j], neighborIdx);
                }
            }
            std::sort(neighbors.rbegin(), neighbors.rend());  // 降序
            
            for (const auto& [weight, neighborIdx] : neighbors) {
                if (coarsestGraph[neighborIdx].partitionId == ~0u &&
                    partitionSizes[p] + coarsestGraph[neighborIdx].totalArea <= maxSize) {
                    
                    coarsestGraph[neighborIdx].partitionId = p;
                    partitionSizes[p] += coarsestGraph[neighborIdx].totalArea;
                    queues[p].push(neighborIdx);
                    progress = true;
                }
            }
            
            // 如果当前节点还有未分配的邻居，放回队�?
            bool hasUnassigned = false;
            for (uint32_t neighbor : coarsestGraph[current].neighbors) {
                if (coarsestGraph[neighbor].partitionId == ~0u) {
                    hasUnassigned = true;
                    break;
                }
            }
            if (hasUnassigned && partitionSizes[p] < maxSize) {
                queues[p].push(current);
            }
        }
    }
    
    // 处理剩余未分配的节点（分配给最近的分区�?
    for (uint32_t i = 0; i < numNodes; ++i) {
        if (coarsestGraph[i].partitionId == ~0u) {
            uint32_t bestPartition = 0;
            float bestDist = std::numeric_limits<float>::max();
            
            for (uint32_t p = 0; p < numPartitions; ++p) {
                float dist = glm::length(coarsestGraph[i].center - coarsestGraph[seeds[p]].center);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestPartition = p;
                }
            }
            
            coarsestGraph[i].partitionId = bestPartition;
        }
    }
}

void MeshClusterizer::projectPartition(const std::vector<CoarseNode>& coarseGraph,
                                        std::vector<CoarseNode>& fineGraph) {
    // 粗图节点包含细图节点的三角形，投影分�?
    // 需要建立三角形到细图节点的映射
    
    std::unordered_map<uint32_t, uint32_t> triangleToFineNode;
    for (size_t i = 0; i < fineGraph.size(); ++i) {
        for (uint32_t triIdx : fineGraph[i].triangles) {
            triangleToFineNode[triIdx] = static_cast<uint32_t>(i);
        }
    }
    
    // 投影：粗节点的分�?-> 包含的三角形 -> 细节�?
    for (const CoarseNode& coarseNode : coarseGraph) {
        for (uint32_t triIdx : coarseNode.triangles) {
            auto it = triangleToFineNode.find(triIdx);
            if (it != triangleToFineNode.end()) {
                fineGraph[it->second].partitionId = coarseNode.partitionId;
            }
        }
    }
}

void MeshClusterizer::refinePartition(std::vector<CoarseNode>& graph) {
    // KL/FM 风格的局部细�?
    // 尝试移动边界节点来减少切边权�?
    
    for (uint32_t iter = 0; iter < m_config.refinementIterations; ++iter) {
        bool improved = false;
        
        for (size_t i = 0; i < graph.size(); ++i) {
            CoarseNode& node = graph[i];
            uint32_t currentPart = node.partitionId;
            
            // 检查是否是边界节点
            bool isBoundary = false;
            std::unordered_map<uint32_t, float> partitionWeights;
            
            for (size_t j = 0; j < node.neighbors.size(); ++j) {
                uint32_t neighborPart = graph[node.neighbors[j]].partitionId;
                partitionWeights[neighborPart] += node.edgeWeights[j];
                
                if (neighborPart != currentPart) {
                    isBoundary = true;
                }
            }
            
            if (!isBoundary) continue;
            
            // 计算移动到其他分区的增益
            float currentGain = partitionWeights[currentPart];
            uint32_t bestPart = currentPart;
            float bestGain = currentGain;
            
            for (const auto& [part, weight] : partitionWeights) {
                if (part != currentPart && weight > bestGain) {
                    bestGain = weight;
                    bestPart = part;
                }
            }
            
            // 如果移动有增益，执行移动
            if (bestPart != currentPart && bestGain > currentGain * 1.1f) {
                node.partitionId = bestPart;
                improved = true;
            }
        }
        
        if (!improved) break;
    }
}

float MeshClusterizer::computeMoveGain(const std::vector<CoarseNode>& graph,
                                        uint32_t nodeIdx,
                                        uint32_t targetPartition) {
    const CoarseNode& node = graph[nodeIdx];
    float gain = 0.0f;
    
    for (size_t j = 0; j < node.neighbors.size(); ++j) {
        uint32_t neighborPart = graph[node.neighbors[j]].partitionId;
        float weight = node.edgeWeights[j];
        
        if (neighborPart == targetPartition) {
            gain += weight;  // 增加内部�?
        } else if (neighborPart == node.partitionId) {
            gain -= weight;  // 减少内部�?
        }
    }
    
    return gain;
}

// ============================================================================
// 步骤 3：边界优�?
// ============================================================================

void MeshClusterizer::optimizePartitionBoundaries() {
    // 进一步优�?Cluster 边界
    // 目标：减�?Cluster 间共享顶点，使边界更平滑
    
    for (uint32_t iter = 0; iter < m_config.boundaryOptimizationIterations; ++iter) {
        bool changed = false;
        
        for (size_t i = 0; i < m_triangles.size(); ++i) {
            TriangleNode& tri = m_triangles[i];
            uint32_t currentPart = tri.partitionId;
            
            // 统计邻居分区
            std::unordered_map<uint32_t, float> neighborPartWeight;
            std::unordered_map<uint32_t, uint32_t> neighborPartCount;
            
            for (size_t j = 0; j < tri.neighbors.size(); ++j) {
                uint32_t neighborPart = m_triangles[tri.neighbors[j]].partitionId;
                neighborPartWeight[neighborPart] += tri.edgeWeights[j];
                neighborPartCount[neighborPart]++;
            }
            
            // 如果多数邻居在其他分区，考虑移动
            if (neighborPartCount.size() > 1) {
                uint32_t bestPart = currentPart;
                float bestScore = neighborPartWeight[currentPart] * 
                                  static_cast<float>(neighborPartCount[currentPart]);
                
                for (const auto& [part, weight] : neighborPartWeight) {
                    float score = weight * static_cast<float>(neighborPartCount[part]);
                    if (part != currentPart && score > bestScore * 1.2f) {
                        bestScore = score;
                        bestPart = part;
                    }
                }
                
                if (bestPart != currentPart) {
                    tri.partitionId = bestPart;
                    changed = true;
                }
            }
        }
        
        if (!changed) break;
    }
}

// ============================================================================
// 步骤 4：生�?Cluster 数据
// ============================================================================

void MeshClusterizer::generateClusterData(const InputMesh& mesh, ClusterizedMesh& output) {
    // ============ 验证分区唯一�?============
    // 确保每个三角形只属于一个分�?
    std::vector<bool> triangleAssigned(m_triangles.size(), false);
    uint32_t unassignedCount = 0;
    uint32_t invalidPartitionCount = 0;
    
    for (size_t i = 0; i < m_triangles.size(); ++i) {
        if (m_triangles[i].partitionId == ~0u) {
            // 未分配的三角形，分配给分�?0
            m_triangles[i].partitionId = 0;
            unassignedCount++;
        }
    }
    
    if (unassignedCount > 0) {
        std::cout << "[MeshClusterizer] WARNING: " << unassignedCount 
                  << " triangles were unassigned, assigned to partition 0" << std::endl;
    }
    
    // 收集每个分区的三角形
    std::unordered_map<uint32_t, std::vector<uint32_t>> partitionTriangles;
    
    for (size_t i = 0; i < m_triangles.size(); ++i) {
        uint32_t partId = m_triangles[i].partitionId;
        partitionTriangles[partId].push_back(static_cast<uint32_t>(i));
        triangleAssigned[i] = true;
    }
    
    // 验证：检查是否有三角形被多次引用（理论上不应该发生）
    size_t totalAssigned = 0;
    for (const auto& [partId, triList] : partitionTriangles) {
        totalAssigned += triList.size();
    }
    
    if (totalAssigned != m_triangles.size()) {
        std::cout << "[MeshClusterizer] ERROR: Triangle count mismatch! "
                  << "Expected " << m_triangles.size() << ", got " << totalAssigned << std::endl;
    }
    
    // 为每个分区创�?Cluster
    output.clusters.reserve(partitionTriangles.size());
    
    uint32_t clusterIdx = 0;
    for (auto& [partId, triIndices] : partitionTriangles) {
        Cluster cluster;
        
        // 收集唯一顶点
        std::unordered_map<uint32_t, uint32_t> globalToLocal;
        
        for (uint32_t triIdx : triIndices) {
            const TriangleNode& tri = m_triangles[triIdx];
            
            for (int i = 0; i < 3; ++i) {
                uint32_t globalIdx = tri.indices[i];
                
                if (globalToLocal.find(globalIdx) == globalToLocal.end()) {
                    uint32_t localIdx = static_cast<uint32_t>(cluster.vertices.size());
                    globalToLocal[globalIdx] = localIdx;
                    
                    Vertex vertex;
                    vertex.position = mesh.positions[globalIdx];
                    vertex.normal = globalIdx < mesh.normals.size() 
                        ? mesh.normals[globalIdx] : glm::vec3(0, 1, 0);
                    vertex.uv = globalIdx < mesh.uvs.size() 
                        ? mesh.uvs[globalIdx] : glm::vec2(0, 0);
                    vertex.tangent = globalIdx < mesh.tangents.size() 
                        ? mesh.tangents[globalIdx] : glm::vec4(1, 0, 0, 1);
                    
                    cluster.vertices.push_back(vertex);
                }
            }
        }
        
        // 生成局部索�?
        cluster.localIndices.reserve(triIndices.size() * 3);
        for (uint32_t triIdx : triIndices) {
            const TriangleNode& tri = m_triangles[triIdx];
            for (int i = 0; i < 3; ++i) {
                uint32_t localIdx = globalToLocal[tri.indices[i]];
                cluster.localIndices.push_back(localIdx);
            }
        }
        
        cluster.triangleCount = static_cast<uint32_t>(triIndices.size());
        cluster.vertexCount = static_cast<uint32_t>(cluster.vertices.size());
        cluster.lodLevel = 0;
        
        // 计算包围�?
        cluster.computeBounds();
        
        // 计算法线�?
        if (m_config.computeNormalCones) {
            cluster.computeNormalCone();
        }
        
        // 压缩顶点
        if (m_config.packVertices) {
            cluster.packVertices();
        }
        
        // 填充 GPU 数据
        cluster.gpuData.boundingSphere = glm::vec4(
            cluster.bounds.center,
            cluster.bounds.radius
        );
        cluster.gpuData.aabbMin = glm::vec4(
            cluster.bounds.aabbMin,
            cluster.bounds.lodError
        );
        cluster.gpuData.aabbMax = glm::vec4(
            cluster.bounds.aabbMax,
            cluster.bounds.screenSizeThreshold
        );
        cluster.gpuData.normalCone = glm::vec4(
            cluster.bounds.coneAxis,
            cluster.bounds.coneAngleCos
        );
        
        cluster.gpuData.vertexOffset = cluster.gpuVertexOffset;
        cluster.gpuData.indexOffset = cluster.gpuIndexOffset;
        cluster.gpuData.triangleCount = cluster.triangleCount;
        cluster.gpuData.lodLevel = cluster.lodLevel;
        
        cluster.gpuData.parentGroupIndex = cluster.parentGroupIndex;
        cluster.gpuData.flags = 1;
        cluster.gpuData.childStartIndex = 0;
        cluster.gpuData.childCount = 0;
        
        output.clusters.push_back(std::move(cluster));
        clusterIdx++;
    }
    
    std::cout << "[MeshClusterizer] Generated " << output.clusters.size() 
              << " clusters from " << m_triangles.size() << " triangles" << std::endl;
}

// ============================================================================
// LOD 生成
// ============================================================================

void MeshClusterizer::generateLODHierarchy(ClusterizedMesh& output) {
    if (!m_config.generateLODs) {
        std::cout << "[MeshClusterizer] LOD generation disabled" << std::endl;
        return;
    }
    
    std::cout << "[MeshClusterizer] Generating LOD hierarchy from " 
              << output.clusters.size() << " LOD0 clusters..." << std::endl;
    
    uint32_t currentLodLevel = 0;
    
    while (true) {
        const auto& currentLod = output.lodLevels[currentLodLevel];
        uint32_t currentClusterCount = currentLod.clusterCount;
        
        if (currentClusterCount <= 4) {
            std::cout << "[MeshClusterizer] LOD generation stopped: only " 
                      << currentClusterCount << " clusters" << std::endl;
            break;
        }
        
        uint32_t totalTriangles = 0;
        for (uint32_t i = currentLod.clusterStartIndex; 
             i < currentLod.clusterStartIndex + currentLod.clusterCount; ++i) {
            totalTriangles += output.clusters[i].triangleCount;
        }
        
        if (totalTriangles < m_config.minLODTriangles) {
            break;
        }
        
        uint32_t nextLodLevel = currentLodLevel + 1;
        uint32_t nextLodStartIndex = static_cast<uint32_t>(output.clusters.size());
        
        std::vector<std::vector<uint32_t>> clusterGroups = groupClustersForLOD(
            output, currentLod.clusterStartIndex, currentLod.clusterCount
        );
        
        if (clusterGroups.empty()) break;
        
        float maxError = 0.0f;
        
        for (const auto& group : clusterGroups) {
            auto simplifiedCluster = simplifyClusterGroup(output, group, nextLodLevel);
            if (simplifiedCluster) {
                for (uint32_t childIdx : group) {
                    output.clusters[childIdx].parentGroupIndex = 
                        static_cast<uint32_t>(output.clusters.size());
                }
                
                maxError = std::max(maxError, simplifiedCluster->bounds.lodError);
                output.clusters.push_back(std::move(*simplifiedCluster));
            }
        }
        
        ClusterizedMesh::LODLevel newLod;
        newLod.clusterStartIndex = nextLodStartIndex;
        newLod.clusterCount = static_cast<uint32_t>(output.clusters.size()) - nextLodStartIndex;
        newLod.maxError = maxError;
        output.lodLevels.push_back(newLod);
        
        std::cout << "[MeshClusterizer] LOD " << nextLodLevel 
                  << ": " << newLod.clusterCount << " clusters" << std::endl;
        
        currentLodLevel = nextLodLevel;
        
        if (currentLodLevel >= 8) break;
    }
    
    updateClusterHierarchyInfo(output);
}

std::vector<std::vector<uint32_t>> MeshClusterizer::groupClustersForLOD(
    const ClusterizedMesh& mesh,
    uint32_t startIndex,
    uint32_t count) 
{
    std::vector<std::vector<uint32_t>> groups;
    
    if (count == 0) return groups;
    
    const uint32_t GROUP_SIZE = 4;
    
    std::vector<bool> assigned(count, false);
    
    for (uint32_t i = 0; i < count; ++i) {
        if (assigned[i]) continue;
        
        std::vector<uint32_t> group;
        group.push_back(startIndex + i);
        assigned[i] = true;
        
        const auto& seed = mesh.clusters[startIndex + i];
        glm::vec3 seedCenter = seed.bounds.center;
        
        std::vector<std::pair<float, uint32_t>> neighbors;
        for (uint32_t j = i + 1; j < count; ++j) {
            if (assigned[j]) continue;
            
            const auto& candidate = mesh.clusters[startIndex + j];
            float dist = glm::length(candidate.bounds.center - seedCenter);
            neighbors.emplace_back(dist, j);
        }
        
        std::sort(neighbors.begin(), neighbors.end());
        
        for (const auto& [dist, idx] : neighbors) {
            if (group.size() >= GROUP_SIZE) break;
            
            group.push_back(startIndex + idx);
            assigned[idx] = true;
        }
        
        groups.push_back(std::move(group));
    }
    
    return groups;
}

std::unique_ptr<Cluster> MeshClusterizer::simplifyClusterGroup(
    const ClusterizedMesh& mesh,
    const std::vector<uint32_t>& clusterIndices,
    uint32_t lodLevel) 
{
    if (clusterIndices.empty()) return nullptr;
    
    std::vector<Vertex> mergedVertices;
    std::vector<uint32_t> mergedIndices;
    
    // 使用精确坐标比较来去重顶点，避免哈希冲突导致错误合并
    // 量化到高精度整数作为 key，同时存储顶点列表处理冲突
    struct VertexKey {
        int32_t x, y, z;
        bool operator==(const VertexKey& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct VertexKeyHash {
        size_t operator()(const VertexKey& k) const {
            size_t h = std::hash<int32_t>()(k.x);
            h ^= std::hash<int32_t>()(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int32_t>()(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    
    // 量化精度：使用 100000 倍（即 0.00001 精度），远高于之前的 1000
    const float VERTEX_QUANTIZE = 100000.0f;
    const float VERTEX_MERGE_EPSILON = 1e-5f;
    
    // key -> list of (mergedIndex) 处理哈希冲突
    std::unordered_map<VertexKey, std::vector<uint32_t>, VertexKeyHash> vertexMap;
    
    auto makeVertexKey = [&](const glm::vec3& pos) -> VertexKey {
        return {
            static_cast<int32_t>(std::round(pos.x * VERTEX_QUANTIZE)),
            static_cast<int32_t>(std::round(pos.y * VERTEX_QUANTIZE)),
            static_cast<int32_t>(std::round(pos.z * VERTEX_QUANTIZE))
        };
    };
    
    auto findExistingVertex = [&](const glm::vec3& pos, const VertexKey& key) -> int32_t {
        auto it = vertexMap.find(key);
        if (it == vertexMap.end()) return -1;
        for (uint32_t idx : it->second) {
            const glm::vec3& existing = mergedVertices[idx].position;
            if (std::abs(existing.x - pos.x) < VERTEX_MERGE_EPSILON &&
                std::abs(existing.y - pos.y) < VERTEX_MERGE_EPSILON &&
                std::abs(existing.z - pos.z) < VERTEX_MERGE_EPSILON) {
                return static_cast<int32_t>(idx);
            }
        }
        return -1;
    };
    
    for (uint32_t clusterIdx : clusterIndices) {
        const auto& cluster = mesh.clusters[clusterIdx];
        
        std::vector<uint32_t> localToGlobal(cluster.vertices.size());
        
        for (uint32_t vi = 0; vi < cluster.vertices.size(); ++vi) {
            const auto& v = cluster.vertices[vi];
            VertexKey key = makeVertexKey(v.position);
            
            int32_t existingIdx = findExistingVertex(v.position, key);
            if (existingIdx >= 0) {
                localToGlobal[vi] = static_cast<uint32_t>(existingIdx);
            } else {
                uint32_t newIdx = static_cast<uint32_t>(mergedVertices.size());
                mergedVertices.push_back(v);
                vertexMap[key].push_back(newIdx);
                localToGlobal[vi] = newIdx;
            }
        }
        
        for (uint32_t localIdx : cluster.localIndices) {
            mergedIndices.push_back(localToGlobal[localIdx]);
        }
    }
    
    uint32_t originalTriangles = static_cast<uint32_t>(mergedIndices.size() / 3);
    uint32_t targetTriangles = std::max(
        static_cast<uint32_t>(originalTriangles * m_config.lodReductionRatio),
        m_config.minTrianglesPerCluster
    );
    
    // ================================================================
    // 识别 Group 边界顶点（Nanite 核心：边界顶点锁定）
    //
    // 边界顶点 = 同时出现在 Group 内 cluster 和 Group 外 cluster 中的顶点
    // 这些顶点在简化时必须被锁定（不允许移动），
    // 否则相邻 Group 简化后边界不再对齐，产生缝隙
    // ================================================================
    // 收集 Group 内所有顶点的位置（用于边界检测）
    // 使用 VertexKey 来做高精度去重
    std::unordered_set<VertexKey, VertexKeyHash> groupVertexKeys;
    for (uint32_t clusterIdx : clusterIndices) {
        const auto& cluster = mesh.clusters[clusterIdx];
        for (const auto& v : cluster.vertices) {
            groupVertexKeys.insert(makeVertexKey(v.position));
        }
    }
    
    // 收集 Group 外部 cluster 中也出现在 Group 内的顶点 key（同一 LOD 级别）
    std::unordered_set<VertexKey, VertexKeyHash> externalVertexKeys;
    std::unordered_set<uint32_t> groupSet(clusterIndices.begin(), clusterIndices.end());
    
    uint32_t currentLOD = mesh.clusters[clusterIndices[0]].lodLevel;
    for (uint32_t i = 0; i < mesh.clusters.size(); ++i) {
        if (mesh.clusters[i].lodLevel == currentLOD && groupSet.find(i) == groupSet.end()) {
            const auto& cluster = mesh.clusters[i];
            for (const auto& v : cluster.vertices) {
                VertexKey key = makeVertexKey(v.position);
                if (groupVertexKeys.count(key)) {
                    externalVertexKeys.insert(key);
                }
            }
        }
    }
    
    // 标记在 merged mesh 中的锁定顶点索引
    std::unordered_set<uint32_t> lockedVertexIndices;
    for (const auto& [key, idxList] : vertexMap) {
        if (externalVertexKeys.count(key)) {
            for (uint32_t mergedIdx : idxList) {
                lockedVertexIndices.insert(mergedIdx);
            }
        }
    }
    
    MeshSimplifier simplifier;
    SimplifierConfig simplifyConfig;
    simplifyConfig.targetTriangleCount = targetTriangles;
    simplifyConfig.preserveBoundary = true;
    simplifyConfig.maxError = 1e6;
    simplifyConfig.lockedVertices = lockedVertexIndices;
    
    auto simplified = simplifier.simplify(mergedVertices, mergedIndices, simplifyConfig);
    if (!simplified || simplified->indices.empty()) {
        return nullptr;
    }
    
    auto newCluster = std::make_unique<Cluster>();
    newCluster->vertices = simplified->vertices;
    newCluster->lodLevel = lodLevel;
    
    // 直接使用所有简化后的索引（不再有 255 顶点限制）
    newCluster->localIndices.reserve(simplified->indices.size());
    for (uint32_t idx : simplified->indices) {
        newCluster->localIndices.push_back(idx);
    }
    
    newCluster->triangleCount = static_cast<uint32_t>(newCluster->localIndices.size() / 3);
    newCluster->vertexCount = static_cast<uint32_t>(newCluster->vertices.size());
    
    newCluster->computeBounds();
    if (m_config.computeNormalCones) {
        newCluster->computeNormalCone();
    }
    
    // ================================================================
    // 累积误差（Nanite 核心算法）
    // 
    // lodError 必须是累积的：
    //   parentError = selfSimplificationError + max(childErrors)
    //
    // 误差值反映真实的 QEM 简化误差。
    // 如果 QEM 误差很小（模型尺寸小或简化质量高），
    // 通过 screenSpaceErrorThreshold 来调节切换灵敏度，
    // 而不是人为放大误差值。
    // ================================================================
    float maxChildLodError = 0.0f;
    for (uint32_t childIdx : clusterIndices) {
        maxChildLodError = std::max(maxChildLodError, mesh.clusters[childIdx].bounds.lodError);
    }
    
    // 纯 QEM 累积误差（Nanite 标准方式）
    newCluster->bounds.lodError = simplified->geometricError + maxChildLodError;
    
    std::cout << "[LOD " << lodLevel << "] lodError=" << newCluster->bounds.lodError
              << " (qemSelf=" << simplified->geometricError
              << ", childMax=" << maxChildLodError
              << ", radius=" << newCluster->bounds.radius << ")" << std::endl;
    
    newCluster->gpuData.boundingSphere = glm::vec4(
        newCluster->bounds.center,
        newCluster->bounds.radius
    );
    newCluster->gpuData.aabbMin = glm::vec4(
        newCluster->bounds.aabbMin,
        newCluster->bounds.lodError
    );
    newCluster->gpuData.aabbMax = glm::vec4(
        newCluster->bounds.aabbMax,
        newCluster->bounds.screenSizeThreshold
    );
    newCluster->gpuData.normalCone = glm::vec4(
        newCluster->bounds.coneAxis,
        newCluster->bounds.coneAngleCos
    );
    newCluster->gpuData.triangleCount = newCluster->triangleCount;
    newCluster->gpuData.lodLevel = lodLevel;
    
    return newCluster;
}

void MeshClusterizer::updateClusterHierarchyInfo(ClusterizedMesh& output) {
    std::unordered_map<uint32_t, std::vector<uint32_t>> parentToChildren;
    
    for (uint32_t i = 0; i < output.clusters.size(); ++i) {
        const auto& cluster = output.clusters[i];
        if (cluster.parentGroupIndex != ~0u && 
            cluster.parentGroupIndex < output.clusters.size()) {
            parentToChildren[cluster.parentGroupIndex].push_back(i);
        }
    }
    
    uint32_t leafCount = 0;
    uint32_t nonLeafCount = 0;
    
    for (uint32_t i = 0; i < output.clusters.size(); ++i) {
        auto& cluster = output.clusters[i];
        auto& gpu = cluster.gpuData;
        
        gpu.flags = 1;  // enabled
        
        bool isLeaf = (cluster.lodLevel == 0);
        auto childIt = parentToChildren.find(i);
        if (childIt == parentToChildren.end() || childIt->second.empty()) {
            isLeaf = true;
        }
        
        if (isLeaf) {
            gpu.flags |= 2;  // mark as leaf
            gpu.childStartIndex = 0;
            gpu.childCount = 0;
            leafCount++;
        } else {
            const auto& children = childIt->second;
            gpu.childStartIndex = children.front();
            gpu.childCount = static_cast<uint32_t>(children.size());
            
            float maxChildError = 0.0f;
            for (uint32_t childIdx : children) {
                maxChildError = std::max(maxChildError, 
                    output.clusters[childIdx].bounds.lodError);
            }
            gpu.aabbMax.w = maxChildError;
            nonLeafCount++;
        }
        
        gpu.parentGroupIndex = cluster.parentGroupIndex;
        gpu.lodLevel = cluster.lodLevel;
    }
    
    std::cout << "[MeshClusterizer] Hierarchy Info:" << std::endl;
    std::cout << "  Total clusters: " << output.clusters.size() << std::endl;
    std::cout << "  Leaf nodes (LOD 0): " << leafCount << std::endl;
    std::cout << "  Non-leaf nodes (higher LOD): " << nonLeafCount << std::endl;
    std::cout << "  LOD levels: " << output.lodLevels.size() << std::endl;
    
    // 打印每个 LOD 级别的详细信�?
    for (size_t lod = 0; lod < output.lodLevels.size(); ++lod) {
        const auto& level = output.lodLevels[lod];
        std::cout << "    LOD " << lod << ": " << level.clusterCount 
                  << " clusters [" << level.clusterStartIndex 
                  << " - " << (level.clusterStartIndex + level.clusterCount - 1) 
                  << "]" << std::endl;
    }
    
    // 打印前几�?cluster 的层级信息以供调�?
    std::cout << "  Sample cluster info:" << std::endl;
    for (uint32_t i = 0; i < std::min((uint32_t)output.clusters.size(), 5u); ++i) {
        const auto& cluster = output.clusters[i];
        std::cout << "    Cluster " << i 
                  << ": LOD=" << cluster.lodLevel
                  << ", parent=" << (cluster.parentGroupIndex == ~0u ? -1 : (int)cluster.parentGroupIndex)
                  << ", flags=" << cluster.gpuData.flags 
                  << std::endl;
    }
    
    // 也打印一些高 LOD �?cluster（如果有�?
    if (output.lodLevels.size() > 1) {
        uint32_t lod1Start = output.lodLevels[1].clusterStartIndex;
        std::cout << "  LOD 1 sample clusters:" << std::endl;
        for (uint32_t i = lod1Start; i < std::min(lod1Start + 3, (uint32_t)output.clusters.size()); ++i) {
            const auto& cluster = output.clusters[i];
            std::cout << "    Cluster " << i 
                      << ": LOD=" << cluster.lodLevel
                      << ", parent=" << (cluster.parentGroupIndex == ~0u ? -1 : (int)cluster.parentGroupIndex)
                      << ", flags=" << cluster.gpuData.flags 
                      << ", childCount=" << cluster.gpuData.childCount
                      << std::endl;
        }
    }
}

} // namespace Nanite
