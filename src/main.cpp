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
// 示例: VulkanPBR --autotest 890 --seconds 20   (聚类 → viz → 换模式 → soak)
int main(int argc, char** argv) {
    try {
        Engine::Config config;
        config.title = "Vulkan PBR Renderer";
        config.width = 1280;
        config.height = 720;

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
            }
        }

        Engine engine(config);
        engine.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
