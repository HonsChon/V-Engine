
#include "InspectorPanel.h"
#include "imgui.h"
#include "../../scene/Scene.h"
#include "../../scene/Entity.h"
#include "../../scene/Components.h"
#include "../../scene/SelectionManager.h"

using namespace VulkanEngine;

InspectorPanel::InspectorPanel() {
}

void InspectorPanel::render() {
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);

    if (m_useECSMode && m_scene) {
        renderECSInspector();
    } else {
        // 旧系统
        if (selectedId == -1) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No object selected");
            ImGui::End();
            return;
        }

        // 对象名称和类型
        ImGui::Text("%s", selectedName.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(%s)", selectedType.c_str());
        ImGui::Separator();

        // 变换组件（所有对象都有）
        renderTransformSection();

        // 根据类型显示不同的组件
        if (selectedType == "Mesh") {
            renderMaterialSection();
        } else if (selectedType == "Light") {
            renderLightSection();
        }
    }

    ImGui::End();
}

void InspectorPanel::setScene(VulkanEngine::Scene* scene) {
    m_scene = scene;
    m_useECSMode = (scene != nullptr);
}

void InspectorPanel::setSelectedEntity(entt::entity entity) {
    m_selectedEntity = entity;
}

// ============================================================
// ECS 模式渲染
// ============================================================

void InspectorPanel::renderECSInspector() {
    if (m_selectedEntity == entt::null) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No entity selected");
        return;
    }

    auto& registry = m_scene->getRegistry();
    
    // 检查实体是否仍然有效
    if (!registry.valid(m_selectedEntity)) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Invalid entity");
        m_selectedEntity = entt::null;
        return;
    }

    // 渲染标签组件（名称）
    renderTagComponent();
    
    ImGui::Separator();

    // 渲染变换组件
    renderTransformComponent();

    // 渲染网格渲染器组件
    renderMeshRendererComponent();

    // 渲染光源组件
    renderLightComponent();

    // 渲染相机组件
    renderCameraComponent();

    ImGui::Separator();

    // 添加组件按钮
    renderAddComponentButton();
}

void InspectorPanel::renderTagComponent() {
    auto& registry = m_scene->getRegistry();
    
    if (registry.all_of<TagComponent>(m_selectedEntity)) {
        auto& tag = registry.get<TagComponent>(m_selectedEntity);
        
        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, tag.tag.c_str(), sizeof(buffer) - 1);
        
        ImGui::Text("Name");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Tag", buffer, sizeof(buffer))) {
            tag.tag = std::string(buffer);
        }

        // 显示 UUID
        if (registry.all_of<UUIDComponent>(m_selectedEntity)) {
            auto& uuid = registry.get<UUIDComponent>(m_selectedEntity);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "UUID: %llu", uuid.uuid);
        }
    }
}

void InspectorPanel::renderTransformComponent() {
    auto& registry = m_scene->getRegistry();
    
    if (!registry.all_of<TransformComponent>(m_selectedEntity)) return;
    
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& transform = registry.get<TransformComponent>(m_selectedEntity);
        
        // Position
        ImGui::Text("Position");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat3("##Position", &transform.position.x, 0.1f);

        // Rotation (欧拉角)
        ImGui::Text("Rotation");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat3("##Rotation", &transform.rotation.x, 1.0f, -360.0f, 360.0f);

        // Scale
        ImGui::Text("Scale");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat3("##Scale", &transform.scale.x, 0.01f, 0.001f, 100.0f);

        // 重置按钮
        if (ImGui::Button("Reset Transform")) {
            transform.position = glm::vec3(0.0f);
            transform.rotation = glm::vec3(0.0f);
            transform.scale = glm::vec3(1.0f);
        }
    }
}

void InspectorPanel::renderMeshRendererComponent() {
    auto& registry = m_scene->getRegistry();
    
    if (!registry.all_of<MeshRendererComponent>(m_selectedEntity)) return;
    
    bool componentOpen = ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen);
    
    // 右键删除组件
    if (ImGui::BeginPopupContextItem("MeshRendererContext")) {
        if (ImGui::MenuItem("Remove Component")) {
            registry.remove<MeshRendererComponent>(m_selectedEntity);
            ImGui::EndPopup();
            return;
        }
        ImGui::EndPopup();
    }
    
    if (componentOpen) {
        auto& meshRenderer = registry.get<MeshRendererComponent>(m_selectedEntity);
        
        // 可见性
        ImGui::Checkbox("Visible", &meshRenderer.visible);
        ImGui::Checkbox("Cast Shadows", &meshRenderer.castShadows);
        ImGui::Checkbox("Receive Shadows", &meshRenderer.receiveShadows);

        // 显示网格和材质路径
        char meshBuffer[256] = {0};
        strncpy(meshBuffer, meshRenderer.meshPath.c_str(), sizeof(meshBuffer) - 1);
        ImGui::Text("Mesh Path");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##MeshPath", meshBuffer, sizeof(meshBuffer))) {
            meshRenderer.meshPath = meshBuffer;
        }

        char matBuffer[256] = {0};
        strncpy(matBuffer, meshRenderer.materialPath.c_str(), sizeof(matBuffer) - 1);
        ImGui::Text("Material");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##MaterialPath", matBuffer, sizeof(matBuffer))) {
            meshRenderer.materialPath = matBuffer;
        }
    }

    // 如果有 PBR 材质组件，显示其属性
    if (registry.all_of<PBRMaterialComponent>(m_selectedEntity)) {
        if (ImGui::CollapsingHeader("PBR Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& material = registry.get<PBRMaterialComponent>(m_selectedEntity);
            
            // Albedo 颜色
            ImGui::Text("Albedo");
            ImGui::SameLine(100);
            ImGui::ColorEdit3("##Albedo", &material.albedo.x);

            // Metallic
            ImGui::Text("Metallic");
            ImGui::SameLine(100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##Metallic", &material.metallic, 0.0f, 1.0f);

            // Roughness
            ImGui::Text("Roughness");
            ImGui::SameLine(100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##Roughness", &material.roughness, 0.0f, 1.0f);

            // AO
            ImGui::Text("AO");
            ImGui::SameLine(100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##AO", &material.ao, 0.0f, 1.0f);

            // Emissive
            ImGui::Text("Emissive");
            ImGui::SameLine(100);
            ImGui::ColorEdit3("##Emissive", &material.emissive.x);

            ImGui::Text("Emissive Str");
            ImGui::SameLine(100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##EmissiveStr", &material.emissiveStrength, 0.0f, 10.0f);
        }
    }
}

void InspectorPanel::renderLightComponent() {
    auto& registry = m_scene->getRegistry();
    
    if (!registry.all_of<LightComponent>(m_selectedEntity)) return;
    
    bool componentOpen = ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen);
    
    // 右键删除组件
    if (ImGui::BeginPopupContextItem("LightContext")) {
        if (ImGui::MenuItem("Remove Component")) {
            registry.remove<LightComponent>(m_selectedEntity);
            ImGui::EndPopup();
            return;
        }
        ImGui::EndPopup();
    }
    
    if (componentOpen) {
        auto& light = registry.get<LightComponent>(m_selectedEntity);
        
        // 光源类型
        const char* lightTypes[] = { "Directional", "Point", "Spot" };
        int currentType = static_cast<int>(light.type);
        ImGui::Text("Type");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##LightType", &currentType, lightTypes, 3)) {
            light.type = static_cast<LightType>(currentType);
        }

        // 颜色
        ImGui::Text("Color");
        ImGui::SameLine(100);
        ImGui::ColorEdit3("##LightColor", &light.color.x);

        // 强度
        ImGui::Text("Intensity");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##Intensity", &light.intensity, 0.0f, 100.0f);

        // 范围（仅对点光源和聚光灯）
        if (light.type != LightType::Directional) {
            ImGui::Text("Range");
            ImGui::SameLine(100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##Range", &light.range, 0.1f, 100.0f);
        }

        // 聚光灯特有属性
        if (light.type == LightType::Spot) {
            ImGui::Text("Inner Angle");
            ImGui::SameLine(100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##InnerAngle", &light.innerConeAngle, 1.0f, light.outerConeAngle);

            ImGui::Text("Outer Angle");
            ImGui::SameLine(100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##OuterAngle", &light.outerConeAngle, light.innerConeAngle, 90.0f);
        }

        // 阴影
        ImGui::Checkbox("Cast Shadows", &light.castShadows);
    }
}

void InspectorPanel::renderCameraComponent() {
    auto& registry = m_scene->getRegistry();
    
    if (!registry.all_of<CameraComponent>(m_selectedEntity)) return;
    
    bool componentOpen = ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen);
    
    // 右键删除组件
    if (ImGui::BeginPopupContextItem("CameraContext")) {
        if (ImGui::MenuItem("Remove Component")) {
            registry.remove<CameraComponent>(m_selectedEntity);
            ImGui::EndPopup();
            return;
        }
        ImGui::EndPopup();
    }
    
    if (componentOpen) {
        auto& camera = registry.get<CameraComponent>(m_selectedEntity);
        
        ImGui::Checkbox("Primary", &camera.isPrimary);
        
        // 投影类型
        const char* projTypes[] = { "Perspective", "Orthographic" };
        int projType = (camera.projectionType == ProjectionType::Orthographic) ? 1 : 0;
        ImGui::Text("Projection");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##ProjectionType", &projType, projTypes, 2)) {
            camera.projectionType = (projType == 1) ? ProjectionType::Orthographic : ProjectionType::Perspective;
        }

        if (camera.projectionType == ProjectionType::Perspective) {
            // 透视投影参数
            ImGui::Text("FOV");
            ImGui::SameLine(100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##FOV", &camera.fov, 0.1f, 3.14f);  // 弧度
        } else {
            // 正交投影参数
            ImGui::Text("Ortho Size");
            ImGui::SameLine(100);
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##OrthoSize", &camera.orthographicSize, 0.1f, 0.1f, 100.0f);
        }

        ImGui::Text("Near Clip");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##NearClip", &camera.nearClip, 0.01f, 0.001f, camera.farClip);

        ImGui::Text("Far Clip");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##FarClip", &camera.farClip, 1.0f, camera.nearClip, 10000.0f);
    }
}

void InspectorPanel::renderAddComponentButton() {
    auto& registry = m_scene->getRegistry();
    
    ImGui::Spacing();
    
    float buttonWidth = 200.0f;
    float windowWidth = ImGui::GetWindowWidth();
    ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
    
    if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!registry.all_of<MeshRendererComponent>(m_selectedEntity)) {
            if (ImGui::MenuItem("Mesh Renderer")) {
                registry.emplace<MeshRendererComponent>(m_selectedEntity);
            }
        }
        if (!registry.all_of<LightComponent>(m_selectedEntity)) {
            if (ImGui::MenuItem("Light")) {
                registry.emplace<LightComponent>(m_selectedEntity);
            }
        }
        if (!registry.all_of<CameraComponent>(m_selectedEntity)) {
            if (ImGui::MenuItem("Camera")) {
                registry.emplace<CameraComponent>(m_selectedEntity);
            }
        }
        ImGui::EndPopup();
    }
}

void InspectorPanel::renderTransformSection() {
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool changed = false;

        // Position
        ImGui::Text("Position");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##Position", &currentTransform.position.x, 0.1f)) {
            changed = true;
        }

        // Rotation
        ImGui::Text("Rotation");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##Rotation", &currentTransform.rotation.x, 1.0f, -360.0f, 360.0f)) {
            changed = true;
        }

        // Scale
        ImGui::Text("Scale");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##Scale", &currentTransform.scale.x, 0.01f, 0.001f, 100.0f)) {
            changed = true;
        }

        // 重置按钮
        if (ImGui::Button("Reset Transform")) {
            currentTransform.position = glm::vec3(0.0f);
            currentTransform.rotation = glm::vec3(0.0f);
            currentTransform.scale = glm::vec3(1.0f);
            changed = true;
        }

        if (changed && onTransformChanged) {
            onTransformChanged(currentTransform);
        }
    }
}

void InspectorPanel::renderMaterialSection() {
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool changed = false;

        // Albedo 颜色
        ImGui::Text("Albedo");
        ImGui::SameLine(100);
        if (ImGui::ColorEdit3("##Albedo", &currentMaterial.albedo.x)) {
            changed = true;
        }

        // Metallic
        ImGui::Text("Metallic");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##Metallic", &currentMaterial.metallic, 0.0f, 1.0f)) {
            changed = true;
        }

        // Roughness
        ImGui::Text("Roughness");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##Roughness", &currentMaterial.roughness, 0.0f, 1.0f)) {
            changed = true;
        }

        // AO
        ImGui::Text("AO");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##AO", &currentMaterial.ao, 0.0f, 1.0f)) {
            changed = true;
        }

        ImGui::Separator();

        // 纹理贴图状态
        ImGui::Text("Texture Maps:");
        ImGui::BulletText("Albedo Map: %s", currentMaterial.hasAlbedoMap ? "Yes" : "No");
        ImGui::BulletText("Normal Map: %s", currentMaterial.hasNormalMap ? "Yes" : "No");
        ImGui::BulletText("Metallic Map: %s", currentMaterial.hasMetallicMap ? "Yes" : "No");
        ImGui::BulletText("Roughness Map: %s", currentMaterial.hasRoughnessMap ? "Yes" : "No");

        if (changed && onMaterialChanged) {
            onMaterialChanged(currentMaterial);
        }
    }
}

void InspectorPanel::renderLightSection() {
    if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        // 光源类型
        const char* lightTypes[] = { "Directional", "Point", "Spot" };
        ImGui::Text("Type");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##LightType", &currentLight.type, lightTypes, 3);

        // 颜色
        ImGui::Text("Color");
        ImGui::SameLine(100);
        ImGui::ColorEdit3("##LightColor", &currentLight.color.x);

        // 强度
        ImGui::Text("Intensity");
        ImGui::SameLine(100);
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##Intensity", &currentLight.intensity, 0.0f, 10.0f);

        // 范围（仅对点光源和聚光灯）
        if (currentLight.type != 0) {
            ImGui::Text("Range");
            ImGui::SameLine(100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##Range", &currentLight.range, 0.1f, 100.0f);
        }
    }
}

void InspectorPanel::setSelectedObject(int id, const std::string& name, const std::string& type) {
    selectedId = id;
    selectedName = name;
    selectedType = type;
}

void InspectorPanel::clearSelection() {
    selectedId = -1;
    selectedName.clear();
    selectedType.clear();
}

void InspectorPanel::setTransform(const Transform& transform) {
    currentTransform = transform;
}

void InspectorPanel::setMaterial(const MaterialData& material) {
    currentMaterial = material;
}

void InspectorPanel::setLight(const LightData& light) {
    currentLight = light;
}
