// 快速占位符实现
#include "Scene.h"
#include "Mesh.h"
#include <iostream>
#include <algorithm>
glm::mat4 Transform::getMatrix() const { 
    glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 r = glm::rotate(glm::mat4(1.0f), rotation.x, glm::vec3(1,0,0)) * glm::rotate(glm::mat4(1.0f), rotation.y, glm::vec3(0,1,0)) * glm::rotate(glm::mat4(1.0f), rotation.z, glm::vec3(0,0,1));
    glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
    return t * r * s; 
}
glm::mat4 Transform::getNormalMatrix() const { return glm::transpose(glm::inverse(getMatrix())); }
SceneObject::SceneObject(std::shared_ptr<Mesh> mesh) : mesh(mesh) {}
Scene::Scene(std::shared_ptr<VulkanDevice> device) : device(device) { std::cout << "Scene placeholder" << std::endl; }
Scene::~Scene() {}
void Scene::addObject(std::shared_ptr<SceneObject> object) { objects.push_back(object); }
void Scene::removeObject(std::shared_ptr<SceneObject> object) {}
void Scene::createDemoScene() {}