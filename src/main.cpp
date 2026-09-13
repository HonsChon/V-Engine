#include "Engine.h"
#include <GLFW/glfw3.h>
#include <cstring>
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
// 示例: VulkanPBR --autotest 890 --seconds 20   (聚类 → viz → 换模式 → soak)
//       VulkanPBR --scene assets/scenes/a.vscene --seconds 5
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
            }
        }

        Engine engine(config);

        // 启动时场景操作（在主循环开始前完成，用于自动化验证）
        if (!scenePath.empty()) engine.openSceneFromFile(scenePath);
        if (!importPath.empty()) engine.importModelFile(importPath);
        if (!savePath.empty()) engine.saveSceneToPath(savePath);

        engine.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
