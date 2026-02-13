
#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <functional>

/**
 * AssetBrowserPanel - 资源浏览器面板
 * 
 * 浏览项目的资源文件（纹理、模型等），支持预览和拖拽加载。
 */
class AssetBrowserPanel {
public:
    // 资源类型
    enum class AssetType {
        Unknown,
        Texture,
        Model,
        Shader,
        Material,
        Scene,
        Folder
    };

    // 资源项
    struct AssetItem {
        std::string name;
        std::string path;
        AssetType type;
        bool isDirectory;
    };

    AssetBrowserPanel();
    ~AssetBrowserPanel() = default;

    /**
     * 渲染面板
     */
    void render();

    /**
     * 设置资源根目录
     */
    void setRootPath(const std::string& path);

    /**
     * 刷新当前目录
     */
    void refresh();

    /**
     * 设置资源双击时的回调
     */
    void setOnAssetDoubleClicked(std::function<void(const std::string&, AssetType)> callback) {
        onAssetDoubleClicked = callback;
    }

    /**
     * 设置资源拖拽时的回调
     */
    void setOnAssetDragged(std::function<void(const std::string&, AssetType)> callback) {
        onAssetDragged = callback;
    }

private:
    void scanDirectory(const std::string& path);
    AssetType getAssetType(const std::string& extension);
    const char* getAssetIcon(AssetType type);
    void navigateToDirectory(const std::string& path);

    std::string rootPath;
    std::string currentPath;
    std::vector<AssetItem> currentItems;

    // 图标大小
    float iconSize = 80.0f;
    float padding = 16.0f;

    // 回调
    std::function<void(const std::string&, AssetType)> onAssetDoubleClicked;
    std::function<void(const std::string&, AssetType)> onAssetDragged;

    // 搜索过滤
    char searchFilter[128] = "";
};
