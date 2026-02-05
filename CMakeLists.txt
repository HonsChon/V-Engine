cmake_minimum_required(VERSION 3.16)
project(VulkanPBR VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# Include directories
include_directories(${Vulkan_INCLUDE_DIRS})
include_directories(src)
include_directories(external)

# Source files
set(SOURCES
    src/main.cpp
    src/VulkanRenderer.cpp
    src/VulkanDevice.cpp
    src/VulkanSwapChain.cpp
    src/VulkanBuffer.cpp
    src/VulkanTexture.cpp
    src/VulkanPipeline.cpp
    src/Mesh.cpp
    src/Material.cpp
    src/Camera.cpp
    src/Scene.cpp
    src/Utils.cpp
)

# Header files
set(HEADERS
    src/VulkanRenderer.h
    src/VulkanDevice.h
    src/VulkanSwapChain.h
    src/VulkanBuffer.h
    src/VulkanTexture.h
    src/VulkanPipeline.h
    src/Mesh.h
    src/Material.h
    src/Camera.h
    src/Scene.h
    src/Utils.h
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    ${Vulkan_LIBRARIES}
    glfw
    glm::glm
)

# Link macOS system frameworks
if(APPLE)
    target_link_libraries(${PROJECT_NAME} 
        "-framework Cocoa"
        "-framework OpenGL" 
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreGraphics"
        "-framework CoreFoundation"
    )
endif()

# Compile definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VK_USE_PLATFORM_MACOS_MVK  # For macOS with MoltenVK
    GLFW_INCLUDE_VULKAN
)

# Copy shaders to build directory
file(COPY ${CMAKE_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})