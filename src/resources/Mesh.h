#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec3 tangent;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(4);

        // Position
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        // Normal
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        // Texture coordinate
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        // Tangent
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, tangent);

        return attributeDescriptions;
    }
    
    // 用于去重的比较运算符
    bool operator==(const Vertex& other) const {
        return pos == other.pos && normal == other.normal && 
               texCoord == other.texCoord && tangent == other.tangent;
    }
};

// Vertex 的哈希函数（用于 unordered_map）
namespace std {
    template<> struct hash<Vertex> {
        size_t operator()(Vertex const& vertex) const {
            size_t h1 = hash<float>()(vertex.pos.x) ^ (hash<float>()(vertex.pos.y) << 1) ^ (hash<float>()(vertex.pos.z) << 2);
            size_t h2 = hash<float>()(vertex.normal.x) ^ (hash<float>()(vertex.normal.y) << 1) ^ (hash<float>()(vertex.normal.z) << 2);
            size_t h3 = hash<float>()(vertex.texCoord.x) ^ (hash<float>()(vertex.texCoord.y) << 1);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}

class Mesh {
public:
    Mesh();
    ~Mesh();

    // 基本几何体创建
    void createCube();
    void createSphere(int segments = 32);
    void createPlane(float size = 10.0f, int subdivisions = 1);
    
    // OBJ 文件加载
    bool loadFromOBJ(const std::string& filepath);
    
    // 清理
    void cleanup();

    // Getters
    const std::vector<Vertex>& getVertices() const { return vertices; }
    const std::vector<uint32_t>& getIndices() const { return indices; }
    const std::string& getName() const { return name; }
    
    // Setters - 用于程序化生成网格
    void setVertices(const std::vector<Vertex>& verts) { 
        vertices = verts; 
        calculateBounds();
    }
    void setIndices(const std::vector<uint32_t>& inds) { indices = inds; }
    void setName(const std::string& n) { name = n; }
    
    // 获取包围盒信息
    glm::vec3 getMinBounds() const { return minBounds; }
    glm::vec3 getMaxBounds() const { return maxBounds; }
    glm::vec3 getCenter() const { return (minBounds + maxBounds) * 0.5f; }
    float getBoundingSphereRadius() const;
    
    // 模型变换（居中和缩放到单位大小）
    void centerAndNormalize();

private:
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::string name;
    
    // 包围盒
    glm::vec3 minBounds;
    glm::vec3 maxBounds;
    
    // 计算包围盒
    void calculateBounds();
    
    // 计算切线空间（用于法线贴图）
    void calculateTangents();
    
    // 如果 OBJ 没有法线，计算顶点法线
    void calculateNormals();
};