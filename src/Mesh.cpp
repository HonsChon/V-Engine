#include "Mesh.h"
#include <stdexcept>
#include <cmath>

const float PI = 3.14159265359f;

Mesh::Mesh() {
    
}

Mesh::~Mesh() {
    
}

void Mesh::createCube() {
    vertices.clear();
    indices.clear();

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
}

void Mesh::createSphere(int segments) {
    vertices.clear();
    indices.clear();

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

    // 生成球体索引
    for (int lat = 0; lat < segments; ++lat) {
        for (int lon = 0; lon < segments; ++lon) {
            uint32_t first = lat * (segments + 1) + lon;
            uint32_t second = first + segments + 1;

            // 第一个三角形
            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            // 第二个三角形
            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }
}

void Mesh::cleanup() {
    vertices.clear();
    indices.clear();
}
