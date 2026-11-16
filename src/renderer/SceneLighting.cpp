#include "SceneLighting.h"

#include "Scene.h"
#include "Components.h"

#include <iostream>

namespace VEngine {

uint32_t collectSceneLights(Scene* scene, RasterLight* outLights) {
    uint32_t count = 0;

    if (scene) {
        auto& registry = scene->getRegistry();
        auto lightView = registry.view<TransformComponent, LightComponent>();
        for (auto entity : lightView) {
            if (count >= kMaxRasterLights) {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    std::cout << "[SceneLighting] scene has more than "
                              << kMaxRasterLights
                              << " lights; extra lights are ignored by raster paths"
                              << std::endl;
                }
                break;
            }
            auto& transform = lightView.get<TransformComponent>(entity);
            auto& light = lightView.get<LightComponent>(entity);

            glm::mat4 world = computeWorldMatrix(registry, entity);

            RasterLight rl{};
            rl.colorType = glm::vec4(light.color,
                                     static_cast<float>(static_cast<int>(light.type)));
            rl.positionInt = glm::vec4(glm::vec3(world[3]), light.intensity);
            rl.directionRange = glm::vec4(
                glm::normalize(glm::mat3(world) * transform.getForward()),
                light.range);
            rl.atten = glm::vec4(light.constantAttenuation,
                                 light.linearAttenuation,
                                 light.quadraticAttenuation,
                                 light.innerConeAngle);
            rl.outerPad = glm::vec4(light.outerConeAngle, 0.0f, 0.0f, 0.0f);
            outLights[count++] = rl;
        }
    }

    if (count == 0) {
        // 后备灯：与历史硬编码行为一致（forward/deferred 原为 (5,3,5) + 300·1/d²）
        RasterLight fallback{};
        fallback.colorType = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);   // Point
        fallback.positionInt = glm::vec4(5.0f, 3.0f, 5.0f, 300.0f);
        fallback.directionRange = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);   // range 0 = 无限
        fallback.atten = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);   // 物理 1/d²
        outLights[0] = fallback;
        count = 1;
    }
    return count;
}

} // namespace VEngine
