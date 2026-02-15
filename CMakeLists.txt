cmake_minimum_required(VERSION 3.16)

# ============================================================
# vcpkg 配置 (必须在 project() 之前)
# ============================================================
# 设置 vcpkg triplet
set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "Vcpkg target triplet")

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "D:/vcpkg-master/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()
message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
message(STATUS "Vcpkg triplet: ${VCPKG_TARGET_TRIPLET}")

# ============================================================
# 项目配置
# ============================================================
project(VulkanPBR VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

# ============================================================
# 查找 Vulkan SDK
# ============================================================
find_package(Vulkan REQUIRED)

if(Vulkan_FOUND)
    message(STATUS "Found Vulkan: ${Vulkan_INCLUDE_DIRS}")
    message(STATUS "Vulkan Libraries: ${Vulkan_LIBRARIES}")
else()
    message(FATAL_ERROR "Vulkan SDK not found! Please install Vulkan SDK from https://vulkan.lunarg.com/")
endif()

# ============================================================
# GLFW 配置
# ============================================================
# 方法1: 使用 find_package (如果通过 vcpkg 或系统安装)
find_package(glfw3 CONFIG QUIET)

if(NOT glfw3_FOUND)
    # 方法2: 手动指定 GLFW 路径 (用户需要根据实际情况修改)
    message(STATUS "glfw3 not found via find_package, trying manual configuration...")
    
    # 用户可以设置 GLFW_DIR 环境变量或 CMake 变量
    if(DEFINED ENV{GLFW_DIR})
        set(GLFW_DIR $ENV{GLFW_DIR})
    endif()
    
    if(GLFW_DIR)
        set(GLFW_INCLUDE_DIR "${GLFW_DIR}/include")
        set(GLFW_LIBRARY "${GLFW_DIR}/lib-vc2022/glfw3.lib")
        
        if(NOT EXISTS ${GLFW_LIBRARY})
            set(GLFW_LIBRARY "${GLFW_DIR}/lib-vc2019/glfw3.lib")
        endif()
        
        message(STATUS "Using GLFW from: ${GLFW_DIR}")
    else()
        message(FATAL_ERROR "GLFW not found! Please install GLFW or set GLFW_DIR")
    endif()
endif()

# ============================================================
# EnTT 配置 (ECS库)
# ============================================================
find_package(EnTT CONFIG REQUIRED)
message(STATUS "Found EnTT via vcpkg")

# ============================================================
# GLM 配置
# ============================================================
find_package(glm CONFIG QUIET)

if(NOT glm_FOUND)
    message(STATUS "glm not found via find_package, trying manual configuration...")
    
    if(DEFINED ENV{GLM_DIR})
        set(GLM_DIR $ENV{GLM_DIR})
    endif()
    
    if(GLM_DIR)
        set(GLM_INCLUDE_DIR "${GLM_DIR}")
        message(STATUS "Using GLM from: ${GLM_DIR}")
    else()
        message(FATAL_ERROR "GLM not found! Please install GLM or set GLM_DIR")
    endif()
endif()

# ============================================================
# Include directories
# ============================================================
include_directories(${Vulkan_INCLUDE_DIRS})
include_directories(src)
include_directories(src/core)
include_directories(src/passes)
include_directories(src/renderer)
include_directories(src/resources)
include_directories(src/third_party)
include_directories(src/third_party/imgui)
include_directories(src/third_party/imgui/backends)
include_directories(src/ui)

if(DEFINED GLFW_INCLUDE_DIR)
    include_directories(${GLFW_INCLUDE_DIR})
endif()

if(DEFINED GLM_INCLUDE_DIR)
    include_directories(${GLM_INCLUDE_DIR})
endif()

# ============================================================
# Source files -- 按目录组织
# ============================================================

# Core - Vulkan 核心封装
set(CORE_SOURCES
    src/core/VulkanDevice.cpp
    src/core/VulkanSwapChain.cpp
    src/core/VulkanBuffer.cpp
    src/core/VulkanTexture.cpp
    src/core/VulkanPipeline.cpp
    src/core/Utils.cpp
)

set(CORE_HEADERS
    src/core/VulkanDevice.h
    src/core/VulkanSwapChain.h
    src/core/VulkanBuffer.h
    src/core/VulkanTexture.h
    src/core/VulkanPipeline.h
    src/core/Utils.h
)

# Passes - 渲染通道
set(PASSES_SOURCES
    src/passes/GBufferPass.cpp
    src/passes/SSRPass.cpp
    src/passes/WaterPass.cpp
    src/passes/ForwardPass.cpp
    src/passes/LightingPass.cpp
)

set(PASSES_HEADERS
    src/passes/RenderPassBase.h
    src/passes/RenderContext.h
    src/passes/GBufferPass.h
    src/passes/SSRPass.h
    src/passes/WaterPass.h
    src/passes/ForwardPass.h
    src/passes/LightingPass.h
)

# Renderer - 渲染器
set(RENDERER_SOURCES
    src/renderer/VulkanRenderer.cpp
    src/renderer/Camera.cpp
)

set(RENDERER_HEADERS
    src/renderer/VulkanRenderer.h
    src/renderer/Camera.h
)

# Resources - 资源管理
set(RESOURCES_SOURCES
    src/resources/Mesh.cpp
    src/resources/Material.cpp
)

set(RESOURCES_HEADERS
    src/resources/Mesh.h
    src/resources/Material.h
)

# Scene - ECS 场景管理系统
set(SCENE_SOURCES
    src/scene/Scene.cpp
    src/scene/Entity.cpp
    src/scene/SceneManager.cpp
    src/scene/SelectionManager.cpp
)

set(SCENE_HEADERS
    src/scene/Scene.h
    src/scene/Entity.h
    src/scene/SceneManager.h
    src/scene/Components.h
    src/scene/SelectionManager.h
    src/scene/RayPicker.h
)

# Third Party Headers
set(THIRD_PARTY_HEADERS
    src/third_party/stb_image.h
    src/third_party/tiny_obj_loader.h
)

# ImGui - UI 库
set(IMGUI_DIR src/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
)

set(IMGUI_HEADERS
    ${IMGUI_DIR}/imgui.h
    ${IMGUI_DIR}/imgui_internal.h
    ${IMGUI_DIR}/imconfig.h
    ${IMGUI_DIR}/imstb_rectpack.h
    ${IMGUI_DIR}/imstb_textedit.h
    ${IMGUI_DIR}/imstb_truetype.h
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.h
    ${IMGUI_DIR}/backends/imgui_impl_glfw.h
)

# UI - 编辑器界面
set(UI_SOURCES
    src/ui/ImGuiLayer.cpp
    src/ui/UIManager.cpp
    src/ui/panels/DebugPanel.cpp
    src/ui/panels/SceneHierarchyPanel.cpp
    src/ui/panels/InspectorPanel.cpp
    src/ui/panels/AssetBrowserPanel.cpp
)

set(UI_HEADERS
    src/ui/ImGuiLayer.h
    src/ui/UIManager.h
    src/ui/panels/DebugPanel.h
    src/ui/panels/SceneHierarchyPanel.h
    src/ui/panels/InspectorPanel.h
    src/ui/panels/AssetBrowserPanel.h
)

# 合并所有源文件
set(ALL_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${PASSES_SOURCES}
    ${RENDERER_SOURCES}
    ${RESOURCES_SOURCES}
    ${SCENE_SOURCES}
    ${IMGUI_SOURCES}
    ${UI_SOURCES}
)

set(ALL_HEADERS
    ${CORE_HEADERS}
    ${PASSES_HEADERS}
    ${RENDERER_HEADERS}
    ${RESOURCES_HEADERS}
    ${SCENE_HEADERS}
    ${THIRD_PARTY_HEADERS}
    ${IMGUI_HEADERS}
    ${UI_HEADERS}
)

# ============================================================
# Create executable
# ============================================================
add_executable(${PROJECT_NAME} ${ALL_SOURCES} ${ALL_HEADERS})

# ============================================================
# Source Groups for IDE (Visual Studio folder structure)
# ============================================================
source_group("Source Files/Core" FILES ${CORE_SOURCES})
source_group("Source Files/Passes" FILES ${PASSES_SOURCES})
source_group("Source Files/Renderer" FILES ${RENDERER_SOURCES})
source_group("Source Files/Resources" FILES ${RESOURCES_SOURCES})
source_group("Header Files/Core" FILES ${CORE_HEADERS})
source_group("Header Files/Passes" FILES ${PASSES_HEADERS})
source_group("Header Files/Renderer" FILES ${RENDERER_HEADERS})
source_group("Header Files/Resources" FILES ${RESOURCES_HEADERS})
source_group("Header Files/Third Party" FILES ${THIRD_PARTY_HEADERS})
source_group("Source Files/ImGui" FILES ${IMGUI_SOURCES})
source_group("Header Files/ImGui" FILES ${IMGUI_HEADERS})
source_group("Source Files/UI" FILES ${UI_SOURCES})
source_group("Header Files/UI" FILES ${UI_HEADERS})
source_group("Source Files/Scene" FILES ${SCENE_SOURCES})
source_group("Header Files/Scene" FILES ${SCENE_HEADERS})

# ============================================================
# Link libraries
# ============================================================
target_link_libraries(${PROJECT_NAME} PRIVATE ${Vulkan_LIBRARIES})
target_link_libraries(${PROJECT_NAME} PRIVATE EnTT::EnTT)

if(glfw3_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE glfw)
elseif(DEFINED GLFW_LIBRARY)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${GLFW_LIBRARY})
endif()

if(glm_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm)
endif()

# ============================================================
# 平台特定配置
# ============================================================
if(WIN32)
    # Windows 平台配置
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        GLFW_INCLUDE_VULKAN
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    
    # Visual Studio 特定设置
    if(MSVC)
        # 设置启动项目
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
        
        # 设置调试工作目录
        set_target_properties(${PROJECT_NAME} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        )
        
        # 编译选项
        target_compile_options(${PROJECT_NAME} PRIVATE /W4 /MP)
        
        # 使用 Unicode
        target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)
    endif()
    
elseif(APPLE)
    # macOS 平台配置
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VK_USE_PLATFORM_MACOS_MVK
        GLFW_INCLUDE_VULKAN
    )
    
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework Cocoa"
        "-framework OpenGL" 
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreGraphics"
        "-framework CoreFoundation"
    )
    
elseif(UNIX)
    # Linux 平台配置
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VK_USE_PLATFORM_XCB_KHR
        GLFW_INCLUDE_VULKAN
    )
endif()

# ============================================================
# Copy shaders and assets to build directory
# ============================================================
# 复制 shaders 目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMENT "Copying shaders to output directory"
)

# 复制 assets 目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
    COMMENT "Copying assets to output directory"
)

# ============================================================
# 编译 Shaders (可选)
# ============================================================
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin")

if(GLSLC)
    message(STATUS "Found glslc: ${GLSLC}")
    
    # Shader 输出目录
    set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders")
    file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})
    
    # 定义 shader 源文件和对应的输出文件名
    set(SHADER_SOURCES
        simple.vert
        simple.frag
        mesh.vert
        mesh.frag
        pbr.vert
        pbr.frag
        simple_mesh.vert
        simple_mesh.frag
        gbuffer.vert
        gbuffer.frag
        ssr.vert
        ssr.frag
        water.vert
        water.frag
        deferred_lighting.vert
        deferred_lighting.frag
    )
    
    foreach(SHADER_FILE ${SHADER_SOURCES})
        set(SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE}")
        
        # 将 simple.vert 转换为 simple_vert.spv 格式
        string(REPLACE "." "_" SHADER_OUTPUT_NAME ${SHADER_FILE})
        set(SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${SHADER_OUTPUT_NAME}.spv")
        
        if(EXISTS ${SHADER_SOURCE})
            add_custom_command(
                OUTPUT ${SHADER_OUTPUT}
                COMMAND ${GLSLC} ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
                DEPENDS ${SHADER_SOURCE}
                COMMENT "Compiling shader: ${SHADER_FILE} -> ${SHADER_OUTPUT_NAME}.spv"
            )
            
            list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
            message(STATUS "  Shader: ${SHADER_FILE} -> ${SHADER_OUTPUT_NAME}.spv")
        else()
            message(WARNING "Shader file not found: ${SHADER_SOURCE}")
        endif()
    endforeach()
    
    if(SHADER_OUTPUTS)
        add_custom_target(CompileShaders ALL DEPENDS ${SHADER_OUTPUTS})
        add_dependencies(${PROJECT_NAME} CompileShaders)
    endif()
else()
    message(WARNING "glslc not found. Shaders will not be compiled automatically.")
    message(WARNING "Please compile shaders manually using: glslc shader.vert -o shader_vert.spv")
endif()

# ============================================================
# 打印配置信息
# ============================================================
message(STATUS "")
message(STATUS "=== VulkanPBR Configuration ===")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "")
message(STATUS "Project Structure:")
message(STATUS "  src/core/       - Vulkan core wrappers")
message(STATUS "  src/passes/     - Render passes (GBuffer, SSR, etc.)")
message(STATUS "  src/renderer/   - Main renderer and camera")
message(STATUS "  src/resources/  - Mesh, Material, Scene")
message(STATUS "  src/third_party/- Third party headers")
message(STATUS "===============================")
message(STATUS "")