#include "Mesh.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <stdexcept>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <algorithm>

const float PI = 3.14159265359f;

Mesh::Mesh() : minBounds(0.0f), maxBounds(0.0f) {
    
}

Mesh::~Mesh() {
    
}

void Mesh::createCube() {
    vertices.clear();
    indices.clear();
    name = "Cube";

    // 立方体顶点数据 (位置, 法线, 纹理坐标, 切线)
    std::vector<Vertex> cubeVertices = {
        // Front face
        {{-1.0f, -1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
        {{-1.0f,  1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},

        // Back face
        {{ 1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
        {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},

        // Left face
        {{-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f,  1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f,  1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},

        // Right face
        {{ 1.0f, -1.0f,  1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},

        // Top face
        {{-1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},

        // Bottom face
        {{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
        {{-1.0f, -1.0f,  1.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}}
    };

    // 立方体索引数据 (每个面2个三角形)
    std::vector<uint32_t> cubeIndices = {
        // Front face
        0, 1, 2, 2, 3, 0,
        // Back face
        4, 5, 6, 6, 7, 4,
        // Left face
        8, 9, 10, 10, 11, 8,
        // Right face
        12, 13, 14, 14, 15, 12,
        // Top face
        16, 17, 18, 18, 19, 16,
        // Bottom face
        20, 21, 22, 22, 23, 20
    };

    vertices = cubeVertices;
    indices = cubeIndices;
    
    calculateBounds();
}

void Mesh::createSphere(int segments) {
    vertices.clear();
    indices.clear();
    name = "Sphere";

    // 球体生成 (UV Sphere)
    for (int lat = 0; lat <= segments; ++lat) {
        float theta = static_cast<float>(lat) * PI / static_cast<float>(segments);
        float sinTheta = sin(theta);
        float cosTheta = cos(theta);

        for (int lon = 0; lon <= segments; ++lon) {
            float phi = static_cast<float>(lon) * 2.0f * PI / static_cast<float>(segments);
            float sinPhi = sin(phi);
            float cosPhi = cos(phi);

            Vertex vertex;
            vertex.pos.x = sinTheta * cosPhi;
            vertex.pos.y = cosTheta;
            vertex.pos.z = sinTheta * sinPhi;

            vertex.normal = glm::normalize(vertex.pos);
            vertex.texCoord.x = static_cast<float>(lon) / static_cast<float>(segments);
            vertex.texCoord.y = static_cast<float>(lat) / static_cast<float>(segments);

            // 简化的切线计算
            vertex.tangent = glm::vec3(-sinPhi, 0.0f, cosPhi);

            vertices.push_back(vertex);
        }
    }

    // 生成球体索引 (修正绕序，使三角形正面朝外)
    for (int lat = 0; lat < segments; ++lat) {
        for (int lon = 0; lon < segments; ++lon) {
            uint32_t first = lat * (segments + 1) + lon;
            uint32_t second = first + segments + 1;

            // 第一个三角形 (逆时针绕序，面朝外)
            indices.push_back(first);
            indices.push_back(first + 1);
            indices.push_back(second);

            // 第二个三角形 (逆时针绕序，面朝外)
            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second + 1);
        }
    }
    
    calculateBounds();
}

void Mesh::createPlane(float size, int subdivisions) {
    vertices.clear();
    indices.clear();
    name = "Plane";
    
    float halfSize = size * 0.5f;
    float step = size / subdivisions;
    
    // 生成顶点
    for (int z = 0; z <= subdivisions; ++z) {
        for (int x = 0; x <= subdivisions; ++x) {
            Vertex vertex;
            vertex.pos = glm::vec3(
                -halfSize + x * step,
                0.0f,
                -halfSize + z * step
            );
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            vertex.texCoord = glm::vec2(
                static_cast<float>(x) / subdivisions,
                static_cast<float>(z) / subdivisions
            );
            vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            
            vertices.push_back(vertex);
        }
    }
    
    // 生成索引
    for (int z = 0; z < subdivisions; ++z) {
        for (int x = 0; x < subdivisions; ++x) {
            uint32_t topLeft = z * (subdivisions + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * (subdivisions + 1) + x;
            uint32_t bottomRight = bottomLeft + 1;
            
            // 第一个三角形
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            
            // 第二个三角形
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }
    
    calculateBounds();
}

bool Mesh::loadFromOBJ(const std::string& filepath) {
    vertices.clear();
    indices.clear();
    
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    
    std::cout << "Loading OBJ file: " << filepath << std::endl;
    
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str())) {
        std::cerr << "Failed to load OBJ file: " << filepath << std::endl;
        if (!err.empty()) {
            std::cerr << "Error: " << err << std::endl;
        }
        return false;
    }
    
    if (!warn.empty()) {
        std::cout << "Warning: " << warn << std::endl;
    }
    
    // 从文件路径提取名称
    size_t lastSlash = filepath.find_last_of("/\\");
    size_t lastDot = filepath.find_last_of('.');
    if (lastSlash != std::string::npos && lastDot != std::string::npos) {
        name = filepath.substr(lastSlash + 1, lastDot - lastSlash - 1);
    } else {
        name = filepath;
    }
    
    std::cout << "Model name: " << name << std::endl;
    std::cout << "Shapes: " << shapes.size() << std::endl;
    std::cout << "Vertices: " << attrib.vertices.size() / 3 << std::endl;
    std::cout << "Normals: " << attrib.normals.size() / 3 << std::endl;
    std::cout << "TexCoords: " << attrib.texcoords.size() / 2 << std::endl;
    
    // 用于顶点去重
    std::unordered_map<Vertex, uint32_t> uniqueVertices;
    
    bool hasNormals = !attrib.normals.empty();
    bool hasTexCoords = !attrib.texcoords.empty();
    
    // 遍历所有形状
    for (const auto& shape : shapes) {
        // 遍历所有面的索引
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};
            
            // 位置
            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };
            
            // 法线
            if (hasNormals && index.normal_index >= 0) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);  // 默认法线，后面会重新计算
            }
            
            // 纹理坐标
            if (hasTexCoords && index.texcoord_index >= 0) {
                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]  // 翻转 V 坐标（OBJ 通常是左下角为原点）
                };
            } else {
                vertex.texCoord = glm::vec2(0.0f, 0.0f);
            }
            
            // 切线暂时设为默认值，后面计算
            vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            
            // 检查是否是重复顶点
            if (uniqueVertices.count(vertex) == 0) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }
            
            indices.push_back(uniqueVertices[vertex]);
        }
    }
    
    std::cout << "Unique vertices: " << vertices.size() << std::endl;
    std::cout << "Indices: " << indices.size() << std::endl;
    
    // 如果没有法线，计算它们
    if (!hasNormals) {
        std::cout << "No normals in OBJ, calculating..." << std::endl;
        calculateNormals();
    }
    
    // 计算切线
    calculateTangents();
    
    // 计算包围盒
    calculateBounds();
    
    std::cout << "Bounds: min(" << minBounds.x << ", " << minBounds.y << ", " << minBounds.z << ")"
              << " max(" << maxBounds.x << ", " << maxBounds.y << ", " << maxBounds.z << ")" << std::endl;
    
    return true;
}

void Mesh::calculateBounds() {
    if (vertices.empty()) {
        minBounds = maxBounds = glm::vec3(0.0f);
        return;
    }
    
    minBounds = maxBounds = vertices[0].pos;
    
    for (const auto& vertex : vertices) {
        minBounds = glm::min(minBounds, vertex.pos);
        maxBounds = glm::max(maxBounds, vertex.pos);
    }
}

float Mesh::getBoundingSphereRadius() const {
    glm::vec3 center = getCenter();
    float maxDist = 0.0f;
    
    for (const auto& vertex : vertices) {
        float dist = glm::length(vertex.pos - center);
        maxDist = std::max(maxDist, dist);
    }
    
    return maxDist;
}

void Mesh::centerAndNormalize() {
    if (vertices.empty()) return;
    
    // 计算中心和大小
    glm::vec3 center = getCenter();
    glm::vec3 size = maxBounds - minBounds;
    float maxDim = std::max({size.x, size.y, size.z});
    
    if (maxDim == 0.0f) return;
    
    // 缩放因子使模型适合单位立方体
    float scale = 2.0f / maxDim;
    
    // 变换所有顶点
    for (auto& vertex : vertices) {
        vertex.pos = (vertex.pos - center) * scale;
    }
    
    // 更新包围盒
    calculateBounds();
    
    std::cout << "Model centered and normalized. New bounds: min(" 
              << minBounds.x << ", " << minBounds.y << ", " << minBounds.z << ")"
              << " max(" << maxBounds.x << ", " << maxBounds.y << ", " << maxBounds.z << ")" << std::endl;
}

void Mesh::calculateNormals() {
    // 重置所有法线为零
    for (auto& vertex : vertices) {
        vertex.normal = glm::vec3(0.0f);
    }
    
    // 计算面法线并累加到顶点
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        
        glm::vec3& v0 = vertices[i0].pos;
        glm::vec3& v1 = vertices[i1].pos;
        glm::vec3& v2 = vertices[i2].pos;
        
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 faceNormal = glm::cross(edge1, edge2);
        
        // 累加到每个顶点（加权平均会自动根据面积加权）
        vertices[i0].normal += faceNormal;
        vertices[i1].normal += faceNormal;
        vertices[i2].normal += faceNormal;
    }
    
    // 归一化所有法线
    for (auto& vertex : vertices) {
        if (glm::length(vertex.normal) > 0.0001f) {
            vertex.normal = glm::normalize(vertex.normal);
        } else {
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
}

void Mesh::calculateTangents() {
    // 重置所有切线
    for (auto& vertex : vertices) {
        vertex.tangent = glm::vec3(0.0f);
    }
    
    // 计算每个三角形的切线并累加
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        
        Vertex& v0 = vertices[i0];
        Vertex& v1 = vertices[i1];
        Vertex& v2 = vertices[i2];
        
        glm::vec3 edge1 = v1.pos - v0.pos;
        glm::vec3 edge2 = v2.pos - v0.pos;
        
        glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
        glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;
        
        float f = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        
        if (std::abs(f) > 0.0001f) {
            f = 1.0f / f;
            
            glm::vec3 tangent;
            tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
            tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
            tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
            
            v0.tangent += tangent;
            v1.tangent += tangent;
            v2.tangent += tangent;
        }
    }
    
    // Gram-Schmidt 正交化并归一化
    for (auto& vertex : vertices) {
        if (glm::length(vertex.tangent) > 0.0001f) {
            // 正交化：T' = T - N * dot(N, T)
            vertex.tangent = glm::normalize(vertex.tangent - vertex.normal * glm::dot(vertex.normal, vertex.tangent));
        } else {
            // 如果切线为零，创建一个默认切线
            if (std::abs(vertex.normal.x) < 0.9f) {
                vertex.tangent = glm::normalize(glm::cross(vertex.normal, glm::vec3(1.0f, 0.0f, 0.0f)));
            } else {
                vertex.tangent = glm::normalize(glm::cross(vertex.normal, glm::vec3(0.0f, 1.0f, 0.0f)));
            }
        }
    }
}

void Mesh::cleanup() {
    vertices.clear();
    indices.clear();
}