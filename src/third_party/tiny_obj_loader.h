/*
The MIT License (MIT)

Copyright (c) 2012-2023 Syoyo Fujita and many contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

// TinyObjLoader - 简化版本，用于加载 OBJ 文件

#ifndef TINY_OBJ_LOADER_H_
#define TINY_OBJ_LOADER_H_

#include <string>
#include <vector>
#include <map>

namespace tinyobj {

// 顶点索引结构
struct index_t {
    int vertex_index;
    int normal_index;
    int texcoord_index;
};

// 网格结构
struct mesh_t {
    std::vector<index_t> indices;
    std::vector<unsigned char> num_face_vertices;  // 每个面的顶点数
    std::vector<int> material_ids;
};

// 形状结构
struct shape_t {
    std::string name;
    mesh_t mesh;
};

// 材质结构
struct material_t {
    std::string name;
    
    float ambient[3];
    float diffuse[3];
    float specular[3];
    float transmittance[3];
    float emission[3];
    float shininess;
    float ior;
    float dissolve;
    int illum;
    
    std::string ambient_texname;
    std::string diffuse_texname;
    std::string specular_texname;
    std::string specular_highlight_texname;
    std::string bump_texname;
    std::string displacement_texname;
    std::string alpha_texname;
    
    // PBR 扩展
    float roughness;
    float metallic;
    float sheen;
    float clearcoat_thickness;
    float clearcoat_roughness;
    float anisotropy;
    float anisotropy_rotation;
    
    std::string roughness_texname;
    std::string metallic_texname;
    std::string sheen_texname;
    std::string emissive_texname;
    std::string normal_texname;
    
    material_t() {
        ambient[0] = 0.0f; ambient[1] = 0.0f; ambient[2] = 0.0f;
        diffuse[0] = 0.0f; diffuse[1] = 0.0f; diffuse[2] = 0.0f;
        specular[0] = 0.0f; specular[1] = 0.0f; specular[2] = 0.0f;
        transmittance[0] = 0.0f; transmittance[1] = 0.0f; transmittance[2] = 0.0f;
        emission[0] = 0.0f; emission[1] = 0.0f; emission[2] = 0.0f;
        shininess = 1.0f;
        ior = 1.0f;
        dissolve = 1.0f;
        illum = 0;
        roughness = 0.0f;
        metallic = 0.0f;
        sheen = 0.0f;
        clearcoat_thickness = 0.0f;
        clearcoat_roughness = 0.0f;
        anisotropy = 0.0f;
        anisotropy_rotation = 0.0f;
    }
};

// 属性结构
struct attrib_t {
    std::vector<float> vertices;   // 'v': 3 floats per vertex
    std::vector<float> normals;    // 'vn': 3 floats per normal
    std::vector<float> texcoords;  // 'vt': 2 floats per texcoord
    std::vector<float> colors;     // vertex colors (extension)
};

// 加载 OBJ 文件
bool LoadObj(attrib_t* attrib, std::vector<shape_t>* shapes,
             std::vector<material_t>* materials, std::string* warn,
             std::string* err, const char* filename,
             const char* mtl_basedir = nullptr, bool triangulate = true);

}  // namespace tinyobj

#ifdef TINYOBJLOADER_IMPLEMENTATION

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <algorithm>

namespace tinyobj {

static inline bool isSpace(const char c) {
    return (c == ' ') || (c == '\t');
}

static inline bool isNewLine(const char c) {
    return (c == '\r') || (c == '\n') || (c == '\0');
}

static inline float parseFloat(const char** token) {
    (*token) += strspn(*token, " \t");
    const char* end = *token + strcspn(*token, " \t\r\n");
    float val = (float)atof(*token);
    *token = end;
    return val;
}

static inline int parseInt(const char** token) {
    (*token) += strspn(*token, " \t");
    const char* end = *token + strcspn(*token, " \t\r\n");
    int val = atoi(*token);
    *token = end;
    return val;
}

static inline void parseFloat2(float* x, float* y, const char** token) {
    *x = parseFloat(token);
    *y = parseFloat(token);
}

static inline void parseFloat3(float* x, float* y, float* z, const char** token) {
    *x = parseFloat(token);
    *y = parseFloat(token);
    *z = parseFloat(token);
}

static inline std::string parseString(const char** token) {
    (*token) += strspn(*token, " \t");
    const char* end = *token + strcspn(*token, " \t\r\n");
    std::string str(*token, end);
    *token = end;
    return str;
}

// 解析 f 行中的顶点索引 (如 "1/2/3" 或 "1//3" 或 "1")
static bool parseTriple(const char** token, int* vIdx, int* vtIdx, int* vnIdx) {
    *vIdx = 0;
    *vtIdx = 0;
    *vnIdx = 0;

    // 跳过空白
    (*token) += strspn(*token, " \t");
    
    const char* end = *token + strcspn(*token, " \t\r\n");
    std::string str(*token, end);
    *token = end;
    
    if (str.empty()) return false;
    
    // 解析 v/vt/vn 格式
    size_t pos1 = str.find('/');
    if (pos1 == std::string::npos) {
        // 只有顶点索引
        *vIdx = atoi(str.c_str());
    } else {
        *vIdx = atoi(str.substr(0, pos1).c_str());
        
        size_t pos2 = str.find('/', pos1 + 1);
        if (pos2 == std::string::npos) {
            // v/vt 格式
            if (pos1 + 1 < str.length()) {
                *vtIdx = atoi(str.substr(pos1 + 1).c_str());
            }
        } else {
            // v/vt/vn 或 v//vn 格式
            if (pos2 > pos1 + 1) {
                *vtIdx = atoi(str.substr(pos1 + 1, pos2 - pos1 - 1).c_str());
            }
            if (pos2 + 1 < str.length()) {
                *vnIdx = atoi(str.substr(pos2 + 1).c_str());
            }
        }
    }
    
    return true;
}

static int fixIndex(int idx, int n) {
    if (idx > 0) return idx - 1;  // OBJ 索引从 1 开始
    if (idx == 0) return 0;
    return n + idx;  // 负索引表示从末尾计数
}

bool LoadObj(attrib_t* attrib, std::vector<shape_t>* shapes,
             std::vector<material_t>* materials, std::string* warn,
             std::string* err, const char* filename,
             const char* mtl_basedir, bool triangulate) {
    
    attrib->vertices.clear();
    attrib->normals.clear();
    attrib->texcoords.clear();
    attrib->colors.clear();
    shapes->clear();
    materials->clear();
    
    std::ifstream ifs(filename);
    if (!ifs) {
        if (err) {
            *err = "Cannot open file: " + std::string(filename);
        }
        return false;
    }
    
    std::string basedir;
    if (mtl_basedir) {
        basedir = mtl_basedir;
    } else {
        std::string filepath(filename);
        size_t pos = filepath.find_last_of("/\\");
        if (pos != std::string::npos) {
            basedir = filepath.substr(0, pos + 1);
        }
    }
    
    shape_t currentShape;
    currentShape.name = "";
    
    std::string line;
    while (std::getline(ifs, line)) {
        // 去除行尾空白
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
            line.pop_back();
        }
        
        if (line.empty() || line[0] == '#') continue;
        
        const char* token = line.c_str();
        token += strspn(token, " \t");
        
        if (token[0] == '\0') continue;
        
        // 顶点位置
        if (token[0] == 'v' && isSpace(token[1])) {
            token += 2;
            float x, y, z;
            parseFloat3(&x, &y, &z, &token);
            attrib->vertices.push_back(x);
            attrib->vertices.push_back(y);
            attrib->vertices.push_back(z);
            continue;
        }
        
        // 纹理坐标
        if (token[0] == 'v' && token[1] == 't' && isSpace(token[2])) {
            token += 3;
            float u, v;
            parseFloat2(&u, &v, &token);
            attrib->texcoords.push_back(u);
            attrib->texcoords.push_back(v);
            continue;
        }
        
        // 法线
        if (token[0] == 'v' && token[1] == 'n' && isSpace(token[2])) {
            token += 3;
            float x, y, z;
            parseFloat3(&x, &y, &z, &token);
            attrib->normals.push_back(x);
            attrib->normals.push_back(y);
            attrib->normals.push_back(z);
            continue;
        }
        
        // 面
        if (token[0] == 'f' && isSpace(token[1])) {
            token += 2;
            
            std::vector<index_t> faceIndices;
            
            while (token[0] != '\0' && !isNewLine(token[0])) {
                int vIdx, vtIdx, vnIdx;
                if (!parseTriple(&token, &vIdx, &vtIdx, &vnIdx)) break;
                
                index_t idx;
                idx.vertex_index = fixIndex(vIdx, (int)attrib->vertices.size() / 3);
                idx.texcoord_index = (vtIdx != 0) ? fixIndex(vtIdx, (int)attrib->texcoords.size() / 2) : -1;
                idx.normal_index = (vnIdx != 0) ? fixIndex(vnIdx, (int)attrib->normals.size() / 3) : -1;
                
                faceIndices.push_back(idx);
            }
            
            // 三角化
            if (triangulate && faceIndices.size() > 3) {
                // Fan triangulation
                for (size_t i = 1; i < faceIndices.size() - 1; i++) {
                    currentShape.mesh.indices.push_back(faceIndices[0]);
                    currentShape.mesh.indices.push_back(faceIndices[i]);
                    currentShape.mesh.indices.push_back(faceIndices[i + 1]);
                    currentShape.mesh.num_face_vertices.push_back(3);
                    currentShape.mesh.material_ids.push_back(-1);
                }
            } else {
                for (const auto& idx : faceIndices) {
                    currentShape.mesh.indices.push_back(idx);
                }
                currentShape.mesh.num_face_vertices.push_back((unsigned char)faceIndices.size());
                currentShape.mesh.material_ids.push_back(-1);
            }
            continue;
        }
        
        // 对象/组名称
        if ((token[0] == 'o' || token[0] == 'g') && isSpace(token[1])) {
            // 保存当前 shape
            if (!currentShape.mesh.indices.empty()) {
                shapes->push_back(currentShape);
            }
            
            token += 2;
            currentShape.name = parseString(&token);
            currentShape.mesh.indices.clear();
            currentShape.mesh.num_face_vertices.clear();
            currentShape.mesh.material_ids.clear();
            continue;
        }
        
        // 忽略其他行 (mtllib, usemtl, s, etc.)
    }
    
    // 保存最后一个 shape
    if (!currentShape.mesh.indices.empty()) {
        if (currentShape.name.empty()) {
            currentShape.name = "default";
        }
        shapes->push_back(currentShape);
    }
    
    return true;
}

}  // namespace tinyobj

#endif  // TINYOBJLOADER_IMPLEMENTATION

#endif  // TINY_OBJ_LOADER_H_
