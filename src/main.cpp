#include "Engine.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// CLI:
//   --autotest 890        键序列(GLFW 数字键码),每 --interval 秒注入一个
//   --interval 3.0        键注入间隔(秒)
//   --seconds 20          序列发完后运行 N 秒自动退出(0 = 不退出)
//   --scene <path>        启动时加载场景文件(.vscene/.json)
//   --import <path>       启动时导入模型(.obj/.gltf/.glb)到场景
//   --save <path>         启动加载/导入完成后保存场景并继续运行(自动化测试用)
//   --cam x,y,z,yaw,pitch 固定初始相机位姿(自动化验证用)
//   --raytrace            离线 CPU 软光追：渲染一帧保存 PNG 后退出（不进主循环）
//   --output <path>       输出路径（默认 raytrace_output.png）
//   --samples N           每像素样本数（默认 64）
//   --bounces N           最大弹射次数（默认 5）
//   --rt-width/--rt-height  输出分辨率（默认 1280x720）
//   --pt                  路径追踪模式（默认 Whitted）
//   --restir              PT + ReSTIR 直接光（覆盖 --pt）
//   --rt-lights N         注入 N 个网格测试点光源（多光源/ReSTIR 测试）
//   --rt-frames N         渲染帧数（时间复用需多帧，结果取平均；默认 1）
//   --rt-shadow-in-target ReSTIR 可见性计入目标函数（更准，更贵）
// 示例: VulkanPBR --autotest 890 --seconds 20   (聚类 → viz → 换模式 → soak)
//       VulkanPBR --scene assets/scenes/a.vscene --seconds 5
//       VulkanPBR --autotest 5 --cam 0,6,4,-90,-55 --seconds 10  (高空俯视水场景)
//       VulkanPBR --scene assets/scenes/sponza.vscene --raytrace --samples 128 --pt
int main(int argc, char** argv) {
    try {
        Engine::Config config;
        config.title = "Vulkan PBR Renderer";
        config.width = 1280;
        config.height = 720;

        std::string scenePath, importPath, savePath;

        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--autotest") == 0 && i + 1 < argc) {
                for (const char* p = argv[++i]; *p; ++p) {
                    if (*p >= '0' && *p <= '9') {
                        config.autotestKeys.push_back(GLFW_KEY_0 + (*p - '0'));
                    }
                }
            } else if (std::strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
                config.autotestInterval = std::stod(argv[++i]);
            } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
                config.autotestSeconds = std::stod(argv[++i]);
            } else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
                scenePath = argv[++i];
            } else if (std::strcmp(argv[i], "--import") == 0 && i + 1 < argc) {
                importPath = argv[++i];
            } else if (std::strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
                savePath = argv[++i];
            } else if (std::strcmp(argv[i], "--raytrace") == 0) {
                config.raytraceOffline = true;
            } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
                config.rtOutputPath = argv[++i];
            } else if (std::strcmp(argv[i], "--samples") == 0 && i + 1 < argc) {
                config.rtSamples = std::max(1, std::stoi(argv[++i]));
            } else if (std::strcmp(argv[i], "--bounces") == 0 && i + 1 < argc) {
                config.rtBounces = std::max(1, std::stoi(argv[++i]));
            } else if (std::strcmp(argv[i], "--rt-width") == 0 && i + 1 < argc) {
                config.rtWidth = std::max(1, std::stoi(argv[++i]));
            } else if (std::strcmp(argv[i], "--rt-height") == 0 && i + 1 < argc) {
                config.rtHeight = std::max(1, std::stoi(argv[++i]));
            } else if (std::strcmp(argv[i], "--pt") == 0) {
                config.rtPathTracing = true;
            } else if (std::strcmp(argv[i], "--restir") == 0) {
                config.rtRestir = true;
            } else if (std::strcmp(argv[i], "--rt-lights") == 0 && i + 1 < argc) {
                config.rtLights = std::max(0, std::stoi(argv[++i]));
            } else if (std::strcmp(argv[i], "--rt-frames") == 0 && i + 1 < argc) {
                config.rtFrames = std::max(1, std::stoi(argv[++i]));
            } else if (std::strcmp(argv[i], "--rt-shadow-in-target") == 0) {
                config.rtShadowInTarget = true;
            } else if (std::strcmp(argv[i], "--cam") == 0 && i + 1 < argc) {
                float v[5] = {0, 0, 5, -90, 0};
                if (std::sscanf(argv[++i], "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]) >= 3) {
                    config.camOverride = true;
                    for (int k = 0; k < 5; ++k) config.camPos[k] = v[k];
                    config.camYaw = v[3]; config.camPitch = v[4];
                }
            }
        }

        Engine engine(config);

        // 启动时场景操作（在主循环开始前完成，用于自动化验证）
        if (!scenePath.empty()) engine.openSceneFromFile(scenePath);
        if (!importPath.empty()) engine.importModelFile(importPath);
        if (!savePath.empty()) engine.saveSceneToPath(savePath);

        // 离线软光追：渲染一帧保存后直接退出（不进主循环）
        if (config.raytraceOffline) {
            return engine.renderOfflineRaytrace() ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        engine.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
