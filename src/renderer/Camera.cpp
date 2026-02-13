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
void Camera::processKeyboard(CameraMovement direction, float deltaTime) {
    // 计算本帧的移动速度
    float velocity = movementSpeed * deltaTime;
    
    // 根据方向移动相机
    switch (direction) {
        case FORWARD:
            position += front * velocity;
            break;
        case BACKWARD:
            position -= front * velocity;
            break;
        case LEFT:
            position -= right * velocity;
            break;
        case RIGHT:
            position += right * velocity;
            break;
        case UP:
            position += worldUp * velocity;
            break;
        case DOWN:
            position -= worldUp * velocity;
            break;
    }
}

void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    // 应用鼠标灵敏度
    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;
    
    // 更新偏航角和俯仰角
    yaw += xoffset;
    pitch += yoffset;
    
    // 限制俯仰角，防止万向节锁和翻转
    if (constrainPitch) {
        if (pitch > MAX_PITCH) {
            pitch = MAX_PITCH;
        }
        if (pitch < -MAX_PITCH) {
            pitch = -MAX_PITCH;
        }
    }
    
    // 根据新的欧拉角更新相机向量
    updateCameraVectors();
}

void Camera::processMouseScroll(float yoffset) {
    // 调整视野（zoom）实现缩放效果
    zoom -= yoffset;
    
    // 限制缩放范围
    if (zoom < MIN_ZOOM) {
        zoom = MIN_ZOOM;
    }
    if (zoom > MAX_ZOOM) {
        zoom = MAX_ZOOM;
    }
}
void Camera::updateCameraVectors() { 
    front = glm::normalize(glm::vec3(cos(glm::radians(yaw)) * cos(glm::radians(pitch)), sin(glm::radians(pitch)), sin(glm::radians(yaw)) * cos(glm::radians(pitch))));
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}