#include "Engine.h"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        Engine::Config config;
        config.title = "Vulkan PBR Renderer";
        config.width = 1280;
        config.height = 720;
        
        Engine engine(config);
        engine.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}