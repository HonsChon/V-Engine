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
include_directories(src/Application)
include_directories(src/RHI)
include_directories(src/Core)
include_directories(src/renderer)
include_directories(src/renderer/passes)
include_directories(src/resources)
include_directories(src/World)
include_directories(src/scene)
include_directories(src/renderer/nanite)
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

# Application - 应用层：生命周期、窗口、输入
set(APPLICATION_SOURCES
    src/Application/Engine.cpp
    src/Application/Window.cpp
    src/Application/Input.cpp
)

set(APPLICATION_HEADERS
    src/Application/Engine.h
    src/Application/Window.h
    src/Application/Input.h
)

# RHI - 渲染硬件接口层：Vulkan封装
set(RHI_SOURCES
    src/RHI/VulkanDevice.cpp
    src/RHI/VulkanSwapChain.cpp
    src/RHI/VulkanBuffer.cpp
    src/RHI/VulkanTexture.cpp
    src/RHI/VulkanPipeline.cpp
    src/RHI/ComputePipeline.cpp
    src/RHI/FrameResources.cpp
)

set(RHI_HEADERS
    src/RHI/VulkanDevice.h
    src/RHI/VulkanSwapChain.h
    src/RHI/VulkanBuffer.h
    src/RHI/VulkanTexture.h
    src/RHI/VulkanPipeline.h
    src/RHI/ComputePipeline.h
    src/RHI/FrameResources.h
)

# Core - 核心工具层：与渲染无关的基础设施
set(CORE_SOURCES
    src/Core/Utils.cpp
)

set(CORE_HEADERS
    src/Core/Utils.h
)

# Renderer Passes - 渲染通道
set(PASSES_SOURCES
    src/renderer/passes/GBufferPass.cpp
    src/renderer/passes/SSRPass.cpp
    src/renderer/passes/WaterPass.cpp
    src/renderer/passes/ForwardPass.cpp
    src/renderer/passes/LightingPass.cpp
    src/renderer/passes/ComputePassBase.cpp
    src/renderer/passes/FrustumCullingPass.cpp
    src/renderer/passes/GPUDrivenRenderer.cpp
    src/renderer/passes/NaniteDebugPass.cpp
    src/renderer/passes/ClusterCullingPass.cpp
)

set(PASSES_HEADERS
    src/renderer/passes/RenderPassBase.h
    src/renderer/passes/RenderContext.h
    src/renderer/passes/GBufferPass.h
    src/renderer/passes/SSRPass.h
    src/renderer/passes/WaterPass.h
    src/renderer/passes/ForwardPass.h
    src/renderer/passes/LightingPass.h
    src/renderer/passes/ComputePassBase.h
    src/renderer/passes/FrustumCullingPass.h
    src/renderer/passes/GPUDrivenRenderer.h
    src/renderer/passes/NaniteDebugPass.h
    src/renderer/passes/ClusterCullingPass.h
)

# Renderer - 渲染器
set(RENDERER_SOURCES
    src/renderer/VulkanRenderer.cpp
    src/renderer/SceneRenderer.cpp
)

set(RENDERER_HEADERS
    src/renderer/VulkanRenderer.h
    src/renderer/SceneRenderer.h
)

# World - 世界/场景层：场景对象
set(WORLD_SOURCES
    src/World/Camera.cpp
)

set(WORLD_HEADERS
    src/World/Camera.h
)

# Resources - 资源管理
set(RESOURCES_SOURCES
    src/resources/Mesh.cpp
    src/resources/Material.cpp
)

set(RESOURCES_HEADERS
    src/resources/Mesh.h
    src/resources/Material.h
    src/resources/MeshManager.h
    src/resources/TextureManager.h
    src/resources/RenderSystem.h
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

# Nanite - 虚拟几何体系统 (moved to renderer/nanite/)
set(NANITE_SOURCES
    src/renderer/nanite/NaniteCluster.cpp
    src/renderer/nanite/MeshClusterizer.cpp
    src/renderer/nanite/MeshSimplifier.cpp
    src/renderer/nanite/NaniteManager.cpp
)

set(NANITE_HEADERS
    src/renderer/nanite/Nanite.h
    src/renderer/nanite/NaniteCluster.h
    src/renderer/nanite/MeshClusterizer.h
    src/renderer/nanite/MeshSimplifier.h
    src/renderer/nanite/NaniteManager.h
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
    ${APPLICATION_SOURCES}
    ${RHI_SOURCES}
    ${CORE_SOURCES}
    ${PASSES_SOURCES}
    ${RENDERER_SOURCES}
    ${WORLD_SOURCES}
    ${RESOURCES_SOURCES}
    ${SCENE_SOURCES}
    ${NANITE_SOURCES}
    ${IMGUI_SOURCES}
    ${UI_SOURCES}
)

set(ALL_HEADERS
    ${APPLICATION_HEADERS}
    ${RHI_HEADERS}
    ${CORE_HEADERS}
    ${PASSES_HEADERS}
    ${RENDERER_HEADERS}
    ${WORLD_HEADERS}
    ${RESOURCES_HEADERS}
    ${SCENE_HEADERS}
    ${NANITE_HEADERS}
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
source_group("Source Files/Application" FILES ${APPLICATION_SOURCES})
source_group("Source Files/RHI" FILES ${RHI_SOURCES})
source_group("Source Files/Core" FILES ${CORE_SOURCES})
source_group("Source Files/Renderer" FILES ${RENDERER_SOURCES})
source_group("Source Files/Renderer/Passes" FILES ${PASSES_SOURCES})
source_group("Source Files/World" FILES ${WORLD_SOURCES})
source_group("Source Files/Resources" FILES ${RESOURCES_SOURCES})
source_group("Source Files/Scene" FILES ${SCENE_SOURCES})
source_group("Source Files/Nanite" FILES ${NANITE_SOURCES})
source_group("Source Files/ImGui" FILES ${IMGUI_SOURCES})
source_group("Source Files/UI" FILES ${UI_SOURCES})

source_group("Header Files/Application" FILES ${APPLICATION_HEADERS})
source_group("Header Files/RHI" FILES ${RHI_HEADERS})
source_group("Header Files/Core" FILES ${CORE_HEADERS})
source_group("Header Files/Renderer" FILES ${RENDERER_HEADERS})
source_group("Header Files/Renderer/Passes" FILES ${PASSES_HEADERS})
source_group("Header Files/World" FILES ${WORLD_HEADERS})
source_group("Header Files/Resources" FILES ${RESOURCES_HEADERS})
source_group("Header Files/Scene" FILES ${SCENE_HEADERS})
source_group("Header Files/Nanite" FILES ${NANITE_HEADERS})
source_group("Header Files/ImGui" FILES ${IMGUI_HEADERS})
source_group("Header Files/UI" FILES ${UI_HEADERS})
source_group("Header Files/Third Party" FILES ${THIRD_PARTY_HEADERS})

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
# 注意：不再复制整个 shaders 目录，因为这会覆盖 glslc 编译好的 .spv 文件
# 着色器由下方的 CompileShaders 目标自动编译
# 如果需要复制源码文件（用于调试），可以单独复制非 .spv 文件

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
    
    # 计算着色器（带子目录）
    set(COMPUTE_SHADER_SOURCES
        culling/frustum_culling.comp
        nanite/cluster_culling.comp
    )
    
    # 编译主目录着色器
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
    
    # 编译计算着色器（保持目录结构）
    foreach(SHADER_FILE ${COMPUTE_SHADER_SOURCES})
        set(SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE}")
        
        # 获取目录部分 (e.g., "culling")
        get_filename_component(SHADER_SUBDIR ${SHADER_FILE} DIRECTORY)
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
        
        # 创建子目录
        if(SHADER_SUBDIR)
            file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}/${SHADER_SUBDIR}")
        endif()
        
        # 输出为 shader.comp.spv 格式
        set(SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${SHADER_FILE}.spv")
        
        if(EXISTS ${SHADER_SOURCE})
            # 同时生成到源目录（程序可能从项目根目录运行）
            set(SHADER_SOURCE_DIR_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE}.spv")
            add_custom_command(
                OUTPUT ${SHADER_OUTPUT}
                COMMAND ${GLSLC} ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
                COMMAND ${CMAKE_COMMAND} -E copy ${SHADER_OUTPUT} ${SHADER_SOURCE_DIR_OUTPUT}
                DEPENDS ${SHADER_SOURCE}
                COMMENT "Compiling compute shader: ${SHADER_FILE} -> ${SHADER_FILE}.spv (+ sync to source)"
            )
            
            list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
            message(STATUS "  Compute Shader: ${SHADER_FILE} -> ${SHADER_FILE}.spv")
        else()
            message(WARNING "Compute shader file not found: ${SHADER_SOURCE}")
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
message(STATUS "  src/Application/ - Engine lifecycle, Window, Input")
message(STATUS "  src/RHI/         - Vulkan abstraction layer")
message(STATUS "  src/Core/        - Utilities (no render dependencies)")
message(STATUS "  src/renderer/    - SceneRenderer, VulkanRenderer")
message(STATUS "  src/renderer/passes/ - Render passes (GBuffer, SSR, etc.)")
message(STATUS "  src/World/       - Camera, Scene objects")
message(STATUS "  src/resources/   - Mesh, Material")
message(STATUS "  src/scene/       - ECS scene management")
message(STATUS "  src/renderer/nanite/ - Virtual geometry system")
message(STATUS "  src/ui/          - Editor UI panels")
message(STATUS "===============================")
message(STATUS "")