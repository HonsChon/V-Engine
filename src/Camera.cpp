// 快速占位符实现
#include "Camera.h"
#include <iostream>
Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch) : position(position), worldUp(up), yaw(yaw), pitch(pitch) { 
    std::cout << "Camera placeholder" << std::endl; 
    updateCameraVectors();
}
glm::mat4 Camera::getViewMatrix() const { return glm::lookAt(position, position + front, up); }
glm::mat4 Camera::getProjectionMatrix(float aspect, float fov, float nearPlane, float farPlane) const { 
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane); 
}
void Camera::processKeyboard(CameraMovement direction, float deltaTime) {}
void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch) {}
void Camera::processMouseScroll(float yoffset) {}
void Camera::updateCameraVectors() { 
    front = glm::normalize(glm::vec3(cos(glm::radians(yaw)) * cos(glm::radians(pitch)), sin(glm::radians(pitch)), sin(glm::radians(yaw)) * cos(glm::radians(pitch))));
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}