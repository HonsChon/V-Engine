#include "SceneManager.h"
#include "Scene.h"

#include <iostream>

namespace VulkanEngine {

SceneManager& SceneManager::getInstance() {
    static SceneManager instance;
    return instance;
}

// ============================================================
// 场景管理
// ============================================================

std::shared_ptr<Scene> SceneManager::createScene(const std::string& name) {
    // 检查是否已存在同名场景
    if (m_scenes.find(name) != m_scenes.end()) {
        std::cerr << "Scene with name '" << name << "' already exists!" << std::endl;
        return m_scenes[name];
    }
    
    auto scene = std::make_shared<Scene>(name);
    m_scenes[name] = scene;
    
    // 如果没有活动场景，将新场景设为活动场景
    if (!m_activeScene) {
        setActiveScene(scene);
    }
    
    return scene;
}

void SceneManager::setActiveScene(std::shared_ptr<Scene> scene) {
    if (m_activeScene == scene) return;
    
    // 停止旧场景
    if (m_activeScene && m_activeScene->isRunning()) {
        m_activeScene->onStop();
    }
    
    m_activeScene = scene;
    
    // 通知监听器
    notifySceneChange();
}

std::shared_ptr<Scene> SceneManager::getScene(const std::string& name) const {
    auto it = m_scenes.find(name);
    if (it != m_scenes.end()) {
        return it->second;
    }
    return nullptr;
}

void SceneManager::unloadScene(const std::string& name) {
    auto it = m_scenes.find(name);
    if (it == m_scenes.end()) {
        std::cerr << "Scene '" << name << "' not found!" << std::endl;
        return;
    }
    
    auto scene = it->second;
    
    // 如果是活动场景，先停止它
    if (scene == m_activeScene) {
        if (scene->isRunning()) {
            scene->onStop();
        }
        m_activeScene = nullptr;
    }
    
    m_scenes.erase(it);
    
    // 如果还有其他场景，选择一个作为活动场景
    if (!m_activeScene && !m_scenes.empty()) {
        setActiveScene(m_scenes.begin()->second);
    }
}

void SceneManager::unloadAllScenes() {
    // 停止活动场景
    if (m_activeScene && m_activeScene->isRunning()) {
        m_activeScene->onStop();
    }
    
    m_activeScene = nullptr;
    m_scenes.clear();
    
    notifySceneChange();
}

bool SceneManager::hasScene(const std::string& name) const {
    return m_scenes.find(name) != m_scenes.end();
}

// ============================================================
// 场景切换
// ============================================================

void SceneManager::switchToScene(const std::string& name, bool async) {
    auto scene = getScene(name);
    if (!scene) {
        std::cerr << "Cannot switch to scene '" << name << "': scene not found!" << std::endl;
        return;
    }
    
    if (async) {
        // TODO: 实现异步加载
        // 这里需要使用线程池或其他异步机制
        std::cerr << "Async scene loading not implemented yet!" << std::endl;
    }
    
    setActiveScene(scene);
}

std::shared_ptr<Scene> SceneManager::loadScene(const std::string& filepath) {
    // TODO: 实现从文件加载场景
    // 这里需要场景序列化系统
    std::cerr << "Scene loading from file not implemented yet!" << std::endl;
    return nullptr;
}

bool SceneManager::saveScene(std::shared_ptr<Scene> scene, const std::string& filepath) {
    // TODO: 实现保存场景到文件
    // 这里需要场景序列化系统
    std::cerr << "Scene saving to file not implemented yet!" << std::endl;
    return false;
}

// ============================================================
// 回调注册
// ============================================================

void SceneManager::onSceneChange(SceneCallback callback) {
    m_sceneChangeCallbacks.push_back(callback);
}

// ============================================================
// 生命周期
// ============================================================

void SceneManager::update(float deltaTime) {
    if (m_activeScene) {
        m_activeScene->onUpdate(deltaTime);
    }
}

void SceneManager::onViewportResize(uint32_t width, uint32_t height) {
    // 更新所有场景的视口大小（或只更新活动场景）
    if (m_activeScene) {
        m_activeScene->onViewportResize(width, height);
    }
}

// ============================================================
// 私有方法
// ============================================================

void SceneManager::notifySceneChange() {
    for (auto& callback : m_sceneChangeCallbacks) {
        callback(m_activeScene);
    }
}

} // namespace VulkanEngine
