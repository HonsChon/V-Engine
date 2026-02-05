#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>

class Mesh;
class VulkanDevice;

struct Transform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    
    glm::mat4 getMatrix() const;
    glm::mat4 getNormalMatrix() const;
};

struct Light {
    glm::vec3 position = glm::vec3(0.0f, 5.0f, 5.0f);
    glm::vec3 color = glm::vec3(23.47f, 21.31f, 20.79f);
    float intensity = 1.0f;
};

class SceneObject {
public:
    SceneObject(std::shared_ptr<Mesh> mesh);
    
    void setTransform(const Transform& transform) { this->transform = transform; }
    Transform& getTransform() { return transform; }
    const Transform& getTransform() const { return transform; }
    
    std::shared_ptr<Mesh> getMesh() const { return mesh; }

private:
    std::shared_ptr<Mesh> mesh;
    Transform transform;
};

class Scene {
public:
    Scene(std::shared_ptr<VulkanDevice> device);
    ~Scene();

    void addObject(std::shared_ptr<SceneObject> object);
    void removeObject(std::shared_ptr<SceneObject> object);
    
    void setLight(const Light& light) { this->light = light; }
    const Light& getLight() const { return light; }
    
    const std::vector<std::shared_ptr<SceneObject>>& getObjects() const { return objects; }
    
    // Create default demo scene
    void createDemoScene();

private:
    std::shared_ptr<VulkanDevice> device;
    std::vector<std::shared_ptr<SceneObject>> objects;
    Light light;
};