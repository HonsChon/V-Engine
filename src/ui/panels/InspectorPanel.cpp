
#include "InspectorPanel.h"
#include "imgui.h"

InspectorPanel::InspectorPanel() {
}

void InspectorPanel::render() {
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);

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

    ImGui::End();
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
