// tinygltf 单头文件库的实现编译单元
// - TINYGLTF_NO_STB_IMAGE: 引擎只需要贴图 URI 路径，不需要解码像素
//   （TextureManager 通过 stb_image 单独加载贴图，避免符号冲突）
// - TINYGLTF_NO_INCLUDE_JSON: 使用 vcpkg 的 nlohmann/json.hpp 而非内嵌路径
// 注意: TINYGLTF_NO_STB_IMAGE / TINYGLTF_NO_STB_IMAGE_WRITE 由 CMake 全局定义,
//       保证所有包含 tiny_gltf.h 的编译单元与实现一致（LoadImageData 符号）。
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_JSON

#pragma warning(push, 0)
#include <nlohmann/json.hpp>
#include "tiny_gltf.h"
#pragma warning(pop)
