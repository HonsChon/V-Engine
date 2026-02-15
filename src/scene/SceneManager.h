#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace VulkanEngine {

class Scene;

/**
 * @brief 场景管理器 - 管理多个场景的创建、切换和销毁
 * 
 * SceneManager 是一个单例类，负责：
 * - 创建和销毁场景
 * - 管理活动场景
 * - 场景切换
 * - 场景生命周期管理
 */
class SceneManager {
public:
    /**
     * @brief 获取单例实例
     */
    static SceneManager& getInstance();

    // 禁止复制和移动
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager(SceneManager&&) = delete;
    SceneManager& operator=(SceneManager&&) = delete;

    // ============================================================
    // 场景管理
    // ============================================================

    /**
     * @brief 创建新场景
     * @param name 场景名称
     * @return 新创建的场景指针
     */
    std::shared_ptr<Scene> createScene(const std::string& name = "Untitled");

    /**
     * @brief 设置活动场景
     * @param scene 要激活的场景
     */
    void setActiveScene(std::shared_ptr<Scene> scene);

    /**
     * @brief 获取活动场景
     * @return 当前活动场景指针
     */
    std::shared_ptr<Scene> getActiveScene() const { return m_activeScene; }

    /**
     * @brief 根据名称获取场景
     * @param name 场景名称
     * @return 场景指针，如果没找到返回 nullptr
     */
    std::shared_ptr<Scene> getScene(const std::string& name) const;

    /**
     * @brief 卸载场景
     * @param name 场景名称
     */
    void unloadScene(const std::string& name);

    /**
     * @brief 卸载所有场景
     */
    void unloadAllScenes();

    /**
     * @brief 检查场景是否存在
     * @param name 场景名称
     * @return 是否存在
     */
    bool hasScene(const std::string& name) const;

    // ============================================================
    // 场景切换
    // ============================================================

    /**
     * @brief 切换到指定场景
     * @param name 场景名称
     * @param async 是否异步加载
     */
    void switchToScene(const std::string& name, bool async = false);

    /**
     * @brief 加载场景（从文件）
     * @param filepath 场景文件路径
     * @return 加载的场景指针
     */
    std::shared_ptr<Scene> loadScene(const std::string& filepath);

    /**
     * @brief 保存场景
     * @param scene 要保存的场景
     * @param filepath 保存路径
     * @return 是否保存成功
     */
    bool saveScene(std::shared_ptr<Scene> scene, const std::string& filepath);

    // ============================================================
    // 回调注册
    // ============================================================

    using SceneCallback = std::function<void(std::shared_ptr<Scene>)>;

    /**
     * @brief 注册场景变更回调
     * @param callback 回调函数
     */
    void onSceneChange(SceneCallback callback);

    // ============================================================
    // 生命周期
    // ============================================================

    /**
     * @brief 更新活动场景
     * @param deltaTime 帧间隔时间
     */
    void update(float deltaTime);

    /**
     * @brief 窗口大小变化
     * @param width 新宽度
     * @param height 新高度
     */
    void onViewportResize(uint32_t width, uint32_t height);

private:
    SceneManager() = default;
    ~SceneManager() = default;

    /**
     * @brief 通知所有监听器场景已变更
     */
    void notifySceneChange();

private:
    std::shared_ptr<Scene> m_activeScene;
    std::unordered_map<std::string, std::shared_ptr<Scene>> m_scenes;
    std::vector<SceneCallback> m_sceneChangeCallbacks;
};

} // namespace VulkanEngine
