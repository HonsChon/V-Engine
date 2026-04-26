#include "MeshSimplifier.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace Nanite {

// ============================================
// Quadric 最优位置计算
// ============================================

bool MeshSimplifier::Quadric::computeOptimalPosition(glm::dvec3& outPos) const {
    // 我们需要求解Ax = b，其中
    // A = [a b c]    b = [-d]
    //     [b e f]        [-g]
    //     [c f h]        [-i]
    
    // 使用克莱默法则求解
    double det = a * (e * h - f * f) 
               - b * (b * h - c * f) 
               + c * (b * f - c * e);
    
    // 检查行列式是否接近 0（奇异矩阵）
    const double EPSILON = 1e-10;
    if (std::abs(det) < EPSILON) {
        return false;
    }
    
    double invDet = 1.0 / det;
    
    // 伴随矩阵
    double adj00 = e * h - f * f;
    double adj01 = c * f - b * h;
    double adj02 = b * f - c * e;
    double adj10 = c * f - b * h;
    double adj11 = a * h - c * c;
    double adj12 = b * c - a * f;
    double adj20 = b * f - c * e;
    double adj21 = b * c - a * f;
    double adj22 = a * e - b * b;
    
    outPos.x = invDet * (adj00 * (-d) + adj01 * (-g) + adj02 * (-i));
    outPos.y = invDet * (adj10 * (-d) + adj11 * (-g) + adj12 * (-i));
    outPos.z = invDet * (adj20 * (-d) + adj21 * (-g) + adj22 * (-i));
    
    return true;
}

// ============================================
// 构造函数
// ============================================

MeshSimplifier::MeshSimplifier() {
}

// ============================================
// 主接口
// ============================================

std::unique_ptr<SimplifiedMesh> MeshSimplifier::simplify(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    const SimplifierConfig& config) 
{
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 重置状态
    m_vertices.clear();
    m_faces.clear();
    m_edges.clear();
    m_edgeMap.clear();
    m_lockedVertices.clear();
    while (!m_collapseQueue.empty()) m_collapseQueue.pop();
    m_maxError = 0.0f;
    m_totalError = 0.0;
    m_collapseCount = 0;
    
    // 保存锁定顶点集合
    m_lockedVertices = config.lockedVertices;
    
    // 步骤 1: 初始化数据结构
    initialize(vertices, indices);
    
    if (m_progressCallback) m_progressCallback(0.1f);
    
    // 步骤 2: 构建邻接关系
    buildAdjacency();
    
    if (m_progressCallback) m_progressCallback(0.2f);
    
    // 步骤 3: 计算初始 Quadrics
    computeInitialQuadrics();
    
    if (m_progressCallback) m_progressCallback(0.3f);
    
    // 步骤 4: 识别边界顶
    identifyBoundaryEdges();
    
    if (m_progressCallback) m_progressCallback(0.4f);
    
    // 步骤 5: 初始化折叠队列
    initializeCollapseQueue();
    
    if (m_progressCallback) m_progressCallback(0.5f);
    
    // 步骤 6: 执行简化
    performSimplification(config);
    
    if (m_progressCallback) m_progressCallback(0.9f);
    
    // 步骤 7: 构建输出
    auto result = buildOutput();
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    result->stats.originalVertices = static_cast<uint32_t>(vertices.size());
    result->stats.originalTriangles = static_cast<uint32_t>(indices.size() / 3);
    result->stats.edgeCollapses = m_collapseCount;
    result->stats.maxGeometricError = m_maxError;
    result->stats.avgGeometricError = m_collapseCount > 0 ? 
        static_cast<float>(m_totalError / m_collapseCount) : 0.0f;
    result->stats.timeMs = static_cast<double>(duration.count());
    result->geometricError = m_maxError;
    
    if (m_progressCallback) m_progressCallback(1.0f);
    
    std::cout << "[MeshSimplifier] Simplified: " 
              << result->stats.originalTriangles << " -> " 
              << result->stats.finalTriangles << " triangles ("
              << m_collapseCount << " collapses, "
              << result->stats.timeMs << " ms)" << std::endl;
    
    return result;
}

std::unique_ptr<Cluster> MeshSimplifier::simplifyCluster(
    const Cluster& cluster,
    uint32_t targetTriangles)
{
    // 将Cluster 数据转换为简化器输入格式
    std::vector<uint32_t> indices;
    indices.reserve(cluster.localIndices.size());
    for (uint32_t idx : cluster.localIndices) {
        indices.push_back(static_cast<uint32_t>(idx));
    }
    
    SimplifierConfig config;
    config.targetTriangleCount = targetTriangles;
    config.preserveBoundary = true;
    
    auto simplified = simplify(cluster.vertices, indices, config);
    if (!simplified || simplified->indices.empty()) {
        return nullptr;
    }
    
    // 构建新的 Cluster
    auto newCluster = std::make_unique<Cluster>();
    newCluster->vertices = simplified->vertices;
    newCluster->lodLevel = cluster.lodLevel + 1;
    newCluster->bounds = cluster.bounds;  // 暂时保留原始包围盒
    
    // 转换索引为局部索引
    newCluster->localIndices.reserve(simplified->indices.size());
    for (uint32_t idx : simplified->indices) {
        newCluster->localIndices.push_back(idx);
    }
    
    newCluster->triangleCount = static_cast<uint32_t>(simplified->indices.size() / 3);
    newCluster->vertexCount = static_cast<uint32_t>(simplified->vertices.size());
    
    // 重新计算包围盒
    newCluster->computeBounds();
    
    return newCluster;
}

// ============================================
// 初始化
// ============================================

void MeshSimplifier::initialize(
    const std::vector<Vertex>& vertices, 
    const std::vector<uint32_t>& indices) 
{
    // 初始化顶点
    m_vertices.resize(vertices.size());
    for (size_t i = 0; i < vertices.size(); i++) {
        m_vertices[i].position = glm::dvec3(vertices[i].position);
        m_vertices[i].normal = glm::dvec3(vertices[i].normal);
        m_vertices[i].uv = glm::dvec2(vertices[i].uv);
        m_vertices[i].tangent = glm::dvec4(vertices[i].tangent);
        m_vertices[i].isValid = true;
    }
    
    // 初始化三角形
    m_faces.resize(indices.size() / 3);
    for (size_t i = 0; i < m_faces.size(); i++) {
        m_faces[i].v[0] = indices[i * 3 + 0];
        m_faces[i].v[1] = indices[i * 3 + 1];
        m_faces[i].v[2] = indices[i * 3 + 2];
        m_faces[i].isValid = true;
        
        // 计算三角形法线
        glm::dvec3 p0 = m_vertices[m_faces[i].v[0]].position;
        glm::dvec3 p1 = m_vertices[m_faces[i].v[1]].position;
        glm::dvec3 p2 = m_vertices[m_faces[i].v[2]].position;
        
        glm::dvec3 e1 = p1 - p0;
        glm::dvec3 e2 = p2 - p0;
        m_faces[i].normal = glm::normalize(glm::cross(e1, e2));
    }
    
    m_currentTriangleCount = static_cast<uint32_t>(m_faces.size());
    m_currentVertexCount = static_cast<uint32_t>(m_vertices.size());
}

// ============================================
// 构建邻接关系
// ============================================

void MeshSimplifier::buildAdjacency() {
    // 为每个三角形添加邻接关系
    for (uint32_t faceIdx = 0; faceIdx < m_faces.size(); faceIdx++) {
        const auto& face = m_faces[faceIdx];
        
        for (int j = 0; j < 3; j++) {
            uint32_t v = face.v[j];
            m_vertices[v].adjacentFaces.push_back(faceIdx);
            
            // 创建边
            uint32_t v0 = face.v[j];
            uint32_t v1 = face.v[(j + 1) % 3];
            uint32_t edgeIdx = getOrCreateEdge(v0, v1);
            
            m_edges[edgeIdx].adjacentFaces.push_back(faceIdx);
            
            // 添加到顶点的邻接边列表
            auto& adj0 = m_vertices[v0].adjacentEdges;
            if (std::find(adj0.begin(), adj0.end(), edgeIdx) == adj0.end()) {
                adj0.push_back(edgeIdx);
            }
            auto& adj1 = m_vertices[v1].adjacentEdges;
            if (std::find(adj1.begin(), adj1.end(), edgeIdx) == adj1.end()) {
                adj1.push_back(edgeIdx);
            }
        }
    }
}

uint32_t MeshSimplifier::getOrCreateEdge(uint32_t v0, uint32_t v1) {
    uint64_t key = makeEdgeKey(v0, v1);
    
    auto it = m_edgeMap.find(key);
    if (it != m_edgeMap.end()) {
        return it->second;
    }
    
    uint32_t edgeIdx = static_cast<uint32_t>(m_edges.size());
    m_edges.push_back({});
    
    InternalEdge& edge = m_edges.back();
    edge.v0 = std::min(v0, v1);
    edge.v1 = std::max(v0, v1);
    edge.isValid = true;
    
    m_edgeMap[key] = edgeIdx;
    return edgeIdx;
}

// ============================================
// 计算初始 Quadrics
// ============================================

void MeshSimplifier::computeInitialQuadrics() {
    for (uint32_t faceIdx = 0; faceIdx < m_faces.size(); faceIdx++) {
        // 计算平面方程
        double pa, pb, pc, pd;
        computeFacePlane(faceIdx, pa, pb, pc, pd);
        
        // 创建 Quadric
        Quadric q = Quadric::fromPlane(pa, pb, pc, pd);
        
        // 添加到三角形的三个顶点
        const auto& face = m_faces[faceIdx];
        for (int j = 0; j < 3; j++) {
            m_vertices[face.v[j]].quadric += q;
        }
    }
}

void MeshSimplifier::computeFacePlane(uint32_t faceIndex, 
                                       double& a, double& b, double& c, double& d) {
    const auto& face = m_faces[faceIndex];
    
    glm::dvec3 p0 = m_vertices[face.v[0]].position;
    glm::dvec3 p1 = m_vertices[face.v[1]].position;
    glm::dvec3 p2 = m_vertices[face.v[2]].position;
    
    glm::dvec3 e1 = p1 - p0;
    glm::dvec3 e2 = p2 - p0;
    glm::dvec3 n = glm::cross(e1, e2);
    
    double len = glm::length(n);
    if (len > 1e-10) {
        n /= len;
    }
    
    a = n.x;
    b = n.y;
    c = n.z;
    d = -glm::dot(n, p0);
}

// ============================================
// 识别边界顶
// ============================================

void MeshSimplifier::identifyBoundaryEdges() {
    for (auto& edge : m_edges) {
        // 边界边只有一个邻接三角形
        edge.isBoundary = (edge.adjacentFaces.size() == 1);
        
        if (edge.isBoundary) {
            m_vertices[edge.v0].isBoundary = true;
            m_vertices[edge.v1].isBoundary = true;
        }
    }
    
    // 标记被锁定的顶点（Cluster Group 边界顶点）
    for (uint32_t lockedIdx : m_lockedVertices) {
        if (lockedIdx < m_vertices.size()) {
            m_vertices[lockedIdx].isBoundary = true;  // 标记为边界
        }
    }
}

// ============================================
// 初始化折叠队列
// ============================================

void MeshSimplifier::initializeCollapseQueue() {
    for (uint32_t edgeIdx = 0; edgeIdx < m_edges.size(); edgeIdx++) {
        if (!m_edges[edgeIdx].isValid) continue;
        
        EdgeCollapse collapse = computeEdgeCollapse(edgeIdx);
        if (collapse.error >= 0) {  // 有效的折叠
            m_collapseQueue.push(collapse);
        }
    }
}

MeshSimplifier::EdgeCollapse MeshSimplifier::computeEdgeCollapse(uint32_t edgeIndex) {
    EdgeCollapse collapse;
    collapse.edgeIndex = edgeIndex;
    collapse.error = -1;  // 无效
    
    const auto& edge = m_edges[edgeIndex];
    if (!edge.isValid) return collapse;
    
    const auto& v0 = m_vertices[edge.v0];
    const auto& v1 = m_vertices[edge.v1];
    
    if (!v0.isValid || !v1.isValid) return collapse;
    
    collapse.v0 = edge.v0;
    collapse.v1 = edge.v1;
    
    // 锁定顶点检查：如果两个端点都是锁定的（Group 边界顶点），
    // 完全禁止折叠，否则会导致相邻 Group 之间产生缝隙
    bool v0Locked = m_lockedVertices.count(edge.v0) > 0;
    bool v1Locked = m_lockedVertices.count(edge.v1) > 0;
    
    if (v0Locked && v1Locked) {
        // 两端都是边界顶点，绝对不能折叠
        return collapse;  // error = -1, 无效
    }
    
    // 合并两个顶点的 Quadric
    Quadric combinedQ = v0.quadric + v1.quadric;
    
    // 尝试计算最优位置
    bool foundOptimal = combinedQ.computeOptimalPosition(collapse.optimalPos);
    
    if (!foundOptimal) {
        // 如果无法计算最优位置，使用边的中点
        collapse.optimalPos = (v0.position + v1.position) * 0.5;
    }
    
    // 如果有一端是锁定顶点，折叠到锁定端（保持锁定顶点位置不变）
    if (v0Locked) {
        // v0 被锁定，折叠到 v0 的位置
        collapse.optimalPos = v0.position;
        // 交换使 v0 是保留的端（锁定端保留）
        // collapse.v0 已经是 edge.v0，无需交换
    } else if (v1Locked) {
        // v1 被锁定，折叠到 v1 的位置
        collapse.optimalPos = v1.position;
        // 交换端点使锁定端成为保留端
        std::swap(collapse.v0, collapse.v1);
    }
    
    // 计算误差
    collapse.error = combinedQ.evaluate(collapse.optimalPos);
    
    // 确保误差非负
    if (collapse.error < 0) {
        collapse.error = 0;
    }
    
    // 边界惩罚
    if (edge.isBoundary) {
        collapse.error += 100.0;  // 大惩罚，但不阻止折叠
    }
    
    // 锁定顶点惩罚（如果只有一端锁定，增加惩罚以推迟折叠）
    if (v0Locked || v1Locked) {
        collapse.error += 50.0;
    }
    
    return collapse;
}

// ============================================
// 执行简化
// ============================================

void MeshSimplifier::performSimplification(const SimplifierConfig& config) {
    // 计算目标三角形数量
    uint32_t targetTriangles;
    if (config.targetTriangleCount > 0) {
        targetTriangles = config.targetTriangleCount;
    } else {
        targetTriangles = static_cast<uint32_t>(m_currentTriangleCount * config.targetRatio);
    }
    targetTriangles = std::max(targetTriangles, 1u);
    
    uint32_t initialTriangles = m_currentTriangleCount;
    
    std::cout << "[MeshSimplifier] Target: " << targetTriangles << " triangles" << std::endl;
    
    while (m_currentTriangleCount > targetTriangles && !m_collapseQueue.empty()) {
        // 获取误差最小的折叠
        EdgeCollapse collapse = m_collapseQueue.top();
        m_collapseQueue.pop();
        
        // 检查边是否仍然有效
        if (!m_edges[collapse.edgeIndex].isValid) {
            continue;
        }
        
        // 检查误差限制
        if (collapse.error > config.maxError) {
            break;
        }
        
        // 检查折叠是否会导致拓扑问题
        if (!isCollapseValid(collapse)) {
            continue;
        }
        
        // 执行折叠
        collapseEdge(collapse);
        
        // 更新统计
        m_maxError = std::max(m_maxError, static_cast<float>(collapse.error));
        m_totalError += collapse.error;
        m_collapseCount++;
        
        // 进度回调
        if (m_progressCallback && m_collapseCount % 100 == 0) {
            float progress = 0.5f + 0.4f * (1.0f - 
                static_cast<float>(m_currentTriangleCount - targetTriangles) / 
                static_cast<float>(initialTriangles - targetTriangles));
            m_progressCallback(std::min(progress, 0.9f));
        }
    }
}

bool MeshSimplifier::isCollapseValid(const EdgeCollapse& collapse) {
    // 检查顶点是否有效
    if (!m_vertices[collapse.v0].isValid || !m_vertices[collapse.v1].isValid) {
        return false;
    }
    
    // 检查折叠后是否会产生翻转的三角形
    const auto& v0 = m_vertices[collapse.v0];
    const auto& v1 = m_vertices[collapse.v1];
    
    // 收集会受影响的三角形（不包括将被删除的）
    std::unordered_set<uint32_t> affectedFaces;
    for (uint32_t faceIdx : v0.adjacentFaces) {
        if (m_faces[faceIdx].isValid) {
            const auto& face = m_faces[faceIdx];
            // 检查这个三角形是否同时包含 v0 和v1（将被删除）
            bool hasV1 = (face.v[0] == collapse.v1 || 
                         face.v[1] == collapse.v1 || 
                         face.v[2] == collapse.v1);
            if (!hasV1) {
                affectedFaces.insert(faceIdx);
            }
        }
    }
    
    // 对于每个受影响的三角形，检查折叠后法线是否翻转
    for (uint32_t faceIdx : affectedFaces) {
        const auto& face = m_faces[faceIdx];
        
        // 获取三角形的三个顶点位置
        glm::dvec3 positions[3];
        for (int j = 0; j < 3; j++) {
            if (face.v[j] == collapse.v0) {
                positions[j] = collapse.optimalPos;  // 使用新位置
            } else {
                positions[j] = m_vertices[face.v[j]].position;
            }
        }
        
        // 计算新法线
        glm::dvec3 e1 = positions[1] - positions[0];
        glm::dvec3 e2 = positions[2] - positions[0];
        glm::dvec3 newNormal = glm::cross(e1, e2);
        
        double len = glm::length(newNormal);
        if (len < 1e-10) {
            // 退化三角形
            return false;
        }
        newNormal /= len;
        
        // 检查法线是否翻转（与原法线夹角大于 90 度）
        if (glm::dot(newNormal, face.normal) < 0.0) {
            return false;
        }
    }
    
    return true;
}

void MeshSimplifier::collapseEdge(const EdgeCollapse& collapse) {
    uint32_t v0 = collapse.v0;
    uint32_t v1 = collapse.v1;
    
    // 更新 v0 的位置和属性
    m_vertices[v0].position = collapse.optimalPos;
    
    // 混合法线和UV（简单平均）
    m_vertices[v0].normal = glm::normalize(
        m_vertices[v0].normal + m_vertices[v1].normal
    );
    m_vertices[v0].uv = (m_vertices[v0].uv + m_vertices[v1].uv) * 0.5;
    m_vertices[v0].tangent = glm::normalize(
        m_vertices[v0].tangent + m_vertices[v1].tangent
    );
    
    // 合并 Quadric
    m_vertices[v0].quadric += m_vertices[v1].quadric;
    
    // 删除 v1
    m_vertices[v1].isValid = false;
    m_currentVertexCount--;
    
    // 收集需要删除的三角形和需要更新的三角形
    std::vector<uint32_t> facesToRemove;
    std::vector<uint32_t> facesToUpdate;
    
    for (uint32_t faceIdx : m_vertices[v1].adjacentFaces) {
        if (!m_faces[faceIdx].isValid) continue;
        
        auto& face = m_faces[faceIdx];
        bool hasV0 = false;
        bool hasV1 = false;
        
        for (int j = 0; j < 3; j++) {
            if (face.v[j] == v0) hasV0 = true;
            if (face.v[j] == v1) hasV1 = true;
        }
        
        if (hasV0 && hasV1) {
            // 这个三角形同时包含两个端点，将被删除
            facesToRemove.push_back(faceIdx);
        } else if (hasV1) {
            // 需要将 v1 替换为v0
            facesToUpdate.push_back(faceIdx);
        }
    }
    
    // 删除三角形
    for (uint32_t faceIdx : facesToRemove) {
        m_faces[faceIdx].isValid = false;
        m_currentTriangleCount--;
        
        // 使相关边无效
        const auto& face = m_faces[faceIdx];
        for (int j = 0; j < 3; j++) {
            uint64_t key = makeEdgeKey(face.v[j], face.v[(j + 1) % 3]);
            auto it = m_edgeMap.find(key);
            if (it != m_edgeMap.end()) {
                // 从边的邻接列表中移除这个三角形
                auto& adjFaces = m_edges[it->second].adjacentFaces;
                adjFaces.erase(
                    std::remove(adjFaces.begin(), adjFaces.end(), faceIdx),
                    adjFaces.end()
                );
            }
        }
    }
    
    // 更新三角形
    for (uint32_t faceIdx : facesToUpdate) {
        auto& face = m_faces[faceIdx];
        
        // 将v1 替换为v0
        for (int j = 0; j < 3; j++) {
            if (face.v[j] == v1) {
                // 更新边映射
                uint32_t vPrev = face.v[(j + 2) % 3];
                uint32_t vNext = face.v[(j + 1) % 3];
                
                // 旧边失效
                uint64_t oldKey1 = makeEdgeKey(v1, vPrev);
                uint64_t oldKey2 = makeEdgeKey(v1, vNext);
                
                // 创建新边（如果不存在）
                uint32_t newEdge1 = getOrCreateEdge(v0, vPrev);
                uint32_t newEdge2 = getOrCreateEdge(v0, vNext);
                
                // 添加邻接关系
                if (std::find(m_edges[newEdge1].adjacentFaces.begin(), 
                             m_edges[newEdge1].adjacentFaces.end(), 
                             faceIdx) == m_edges[newEdge1].adjacentFaces.end()) {
                    m_edges[newEdge1].adjacentFaces.push_back(faceIdx);
                }
                if (std::find(m_edges[newEdge2].adjacentFaces.begin(), 
                             m_edges[newEdge2].adjacentFaces.end(), 
                             faceIdx) == m_edges[newEdge2].adjacentFaces.end()) {
                    m_edges[newEdge2].adjacentFaces.push_back(faceIdx);
                }
                
                face.v[j] = v0;
                break;
            }
        }
        
        // 重新计算法线
        glm::dvec3 p0 = m_vertices[face.v[0]].position;
        glm::dvec3 p1 = m_vertices[face.v[1]].position;
        glm::dvec3 p2 = m_vertices[face.v[2]].position;
        
        glm::dvec3 e1 = p1 - p0;
        glm::dvec3 e2 = p2 - p0;
        face.normal = glm::normalize(glm::cross(e1, e2));
        
        // 添加到v0 的邻接三角形
        if (std::find(m_vertices[v0].adjacentFaces.begin(),
                     m_vertices[v0].adjacentFaces.end(),
                     faceIdx) == m_vertices[v0].adjacentFaces.end()) {
            m_vertices[v0].adjacentFaces.push_back(faceIdx);
        }
    }
    
    // 使原始边无效
    m_edges[collapse.edgeIndex].isValid = false;
    
    // 将v1 的邻接边转移到v0，并更新折叠队列
    updateAffectedEdges(v0);
}

void MeshSimplifier::updateAffectedEdges(uint32_t vertexIndex) {
    auto& vertex = m_vertices[vertexIndex];
    
    // 收集需要更新的边
    std::unordered_set<uint32_t> edgesToUpdate;
    
    for (uint32_t faceIdx : vertex.adjacentFaces) {
        if (!m_faces[faceIdx].isValid) continue;
        
        const auto& face = m_faces[faceIdx];
        for (int j = 0; j < 3; j++) {
            uint64_t key = makeEdgeKey(face.v[j], face.v[(j + 1) % 3]);
            auto it = m_edgeMap.find(key);
            if (it != m_edgeMap.end() && m_edges[it->second].isValid) {
                edgesToUpdate.insert(it->second);
            }
        }
    }
    
    // 重新计算这些边的折叠代价并加入队列
    for (uint32_t edgeIdx : edgesToUpdate) {
        EdgeCollapse collapse = computeEdgeCollapse(edgeIdx);
        if (collapse.error >= 0) {
            m_collapseQueue.push(collapse);
        }
    }
}

// ============================================
// 构建输出
// ============================================

std::unique_ptr<SimplifiedMesh> MeshSimplifier::buildOutput() {
    auto result = std::make_unique<SimplifiedMesh>();
    
    // 重映射顶点索引
    uint32_t newIndex = 0;
    for (auto& vertex : m_vertices) {
        if (vertex.isValid) {
            vertex.remappedIndex = newIndex++;
        }
    }
    
    // 输出顶点
    result->vertices.reserve(newIndex);
    for (const auto& vertex : m_vertices) {
        if (vertex.isValid) {
            Vertex v;
            v.position = glm::vec3(vertex.position);
            v.normal = glm::vec3(glm::normalize(vertex.normal));
            v.uv = glm::vec2(vertex.uv);
            v.tangent = glm::vec4(glm::normalize(glm::dvec3(vertex.tangent)), vertex.tangent.w);
            result->vertices.push_back(v);
        }
    }
    
    // 输出三角形
    for (const auto& face : m_faces) {
        if (face.isValid) {
            for (int j = 0; j < 3; j++) {
                result->indices.push_back(m_vertices[face.v[j]].remappedIndex);
            }
        }
    }
    
    result->stats.finalVertices = static_cast<uint32_t>(result->vertices.size());
    result->stats.finalTriangles = static_cast<uint32_t>(result->indices.size() / 3);
    
    return result;
}

} // namespace Nanite
