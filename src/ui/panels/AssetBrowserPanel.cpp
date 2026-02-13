
#include "AssetBrowserPanel.h"
#include "imgui.h"
#include <algorithm>

AssetBrowserPanel::AssetBrowserPanel() {
}

void AssetBrowserPanel::render() {
    ImGui::Begin("Asset Browser", nullptr, ImGuiWindowFlags_NoCollapse);

    // 工具栏
    if (ImGui::Button("Refresh")) {
        refresh();
    }
    ImGui::SameLine();

    // 返回上一级
    if (currentPath != rootPath) {
        if (ImGui::Button("Back")) {
            std::filesystem::path p(currentPath);
            navigateToDirectory(p.parent_path().string());
        }
        ImGui::SameLine();
    }

    // 当前路径显示
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", currentPath.c_str());

    // 搜索框
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##Search", "Search assets...", searchFilter, sizeof(searchFilter));
    ImGui::Separator();

    // 资源网格显示
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = std::max(1, static_cast<int>(panelWidth / (iconSize + padding)));

    ImGui::Columns(columnCount, nullptr, false);

    for (const auto& item : currentItems) {
        // 搜索过滤
        if (strlen(searchFilter) > 0) {
            std::string nameLower = item.name;
            std::string filterLower = searchFilter;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
            if (nameLower.find(filterLower) == std::string::npos) {
                continue;
            }
        }

        ImGui::PushID(item.path.c_str());

        // 图标按钮
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 0.9f));

        if (ImGui::Button(getAssetIcon(item.type), ImVec2(iconSize, iconSize))) {
            // 单击选中
        }

        // 双击打开
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (item.isDirectory) {
                navigateToDirectory(item.path);
            } else if (onAssetDoubleClicked) {
                onAssetDoubleClicked(item.path, item.type);
            }
        }

        // 拖拽支持
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("ASSET_ITEM", item.path.c_str(), item.path.size() + 1);
            ImGui::Text("%s", item.name.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::PopStyleColor(3);

        // 文件名（截断显示）
        std::string displayName = item.name;
        if (displayName.length() > 12) {
            displayName = displayName.substr(0, 10) + "...";
        }
        ImGui::TextWrapped("%s", displayName.c_str());

        ImGui::NextColumn();
        ImGui::PopID();
    }

    ImGui::Columns(1);

    // 显示资源统计
    ImGui::Separator();
    int fileCount = 0, folderCount = 0;
    for (const auto& item : currentItems) {
        if (item.isDirectory) folderCount++;
        else fileCount++;
    }
    ImGui::Text("%d folders, %d files", folderCount, fileCount);

    ImGui::End();
}

void AssetBrowserPanel::setRootPath(const std::string& path) {
    rootPath = path;
    currentPath = path;
    refresh();
}

void AssetBrowserPanel::refresh() {
    scanDirectory(currentPath);
}

void AssetBrowserPanel::scanDirectory(const std::string& path) {
    currentItems.clear();

    if (path.empty() || !std::filesystem::exists(path)) {
        return;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            AssetItem item;
            item.name = entry.path().filename().string();
            item.path = entry.path().string();
            item.isDirectory = entry.is_directory();

            if (item.isDirectory) {
                item.type = AssetType::Folder;
            } else {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                item.type = getAssetType(ext);
            }

            currentItems.push_back(item);
        }

        // 排序：文件夹在前，然后按名称排序
        std::sort(currentItems.begin(), currentItems.end(), 
            [](const AssetItem& a, const AssetItem& b) {
                if (a.isDirectory != b.isDirectory) {
                    return a.isDirectory > b.isDirectory;
                }
                return a.name < b.name;
            });

    } catch (const std::exception& e) {
        // 处理文件系统错误
    }
}

AssetBrowserPanel::AssetType AssetBrowserPanel::getAssetType(const std::string& extension) {
    // 纹理
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
        extension == ".tga" || extension == ".bmp" || extension == ".hdr") {
        return AssetType::Texture;
    }
    // 模型
    if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb") {
        return AssetType::Model;
    }
    // 着色器
    if (extension == ".vert" || extension == ".frag" || extension == ".glsl" ||
        extension == ".spv" || extension == ".comp") {
        return AssetType::Shader;
    }
    // 材质
    if (extension == ".mat" || extension == ".material") {
        return AssetType::Material;
    }
    // 场景
    if (extension == ".scene" || extension == ".json") {
        return AssetType::Scene;
    }

    return AssetType::Unknown;
}

const char* AssetBrowserPanel::getAssetIcon(AssetType type) {
    switch (type) {
        case AssetType::Folder:   return "[D]";
        case AssetType::Texture:  return "[T]";
        case AssetType::Model:    return "[M]";
        case AssetType::Shader:   return "[S]";
        case AssetType::Material: return "[MT]";
        case AssetType::Scene:    return "[SC]";
        default:                  return "[?]";
    }
}

void AssetBrowserPanel::navigateToDirectory(const std::string& path) {
    currentPath = path;
    refresh();
}
