#pragma once

#include <string>

namespace VEngine {

/**
 * @brief 资源路径解析
 *
 * 场景序列化保存的 mesh/贴图路径可能是：
 *   - 相对路径（如 "../../assets/Earth/Maps/Color Map.jpg"，依赖运行目录）
 *   - 绝对路径（拖拽导入的模型）
 * 当场景在另一个工作目录下重新打开时，原路径可能失效。
 * 本函数按以下顺序解析出当前可用的路径：
 *   1. 预设名（sphere/cube/plane、__default_*__、材质ID 含 '|'）直接透传
 *   2. 原样存在 → 原样返回
 *   3. 路径中含 "assets/" 时，截取 assets/ 起始的相对部分，
 *      依次在 CWD、../../、../../../、../ 下查找（覆盖常见的 build/bin 目录布局）
 *   4. 未找到 → 原样返回（由调用方报错）
 *
 * glTF meshId 的 "#mesh_prim" 后缀会被保留。
 *
 * 实现位于 ModelImporter.cpp（依赖 <filesystem>，避免在头文件中引入）。
 */
std::string resolveAssetPath(const std::string& path);

/**
 * @brief 拼接目录与相对路径（path 为绝对路径时原样返回）
 */
std::string joinPath(const std::string& baseDir, const std::string& path);

} // namespace VEngine
