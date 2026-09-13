cmake_minimum_required(VERSION 3.16)

# ============================================================
# vcpkg 配置 (必须在 project() 之前)
# ============================================================
# 自动检测平台设置 vcpkg triplet
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    if(APPLE)
        # macOS: 检测 ARM64 (Apple Silicon) vs x86_64
        execute_process(COMMAND uname -m OUTPUT_VARIABLE HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(HOST_ARCH STREQUAL "arm64")
            set(VCPKG_TARGET_TRIPLET "arm64-osx" CACHE STRING "Vcpkg target triplet")
        else()
            set(VCPKG_TARGET_TRIPLET "x64-osx" CACHE STRING "Vcpkg target triplet")
        endif()
    elseif(WIN32)
        set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "Vcpkg target triplet")
    elseif(UNIX)
        set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "Vcpkg target triplet")
    endif()
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    # 尝试从环境变量 VCPKG_ROOT 查找
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "Vcpkg toolchain file")
    elseif(WIN32)
        set(CMAKE_TOOLCHAIN_FILE "D:/vcpkg-master/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "Vcpkg toolchain file")
    elseif(APPLE)
        # macOS 常见 vcpkg 安装路径
        if(EXISTS "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake"
                CACHE STRING "Vcpkg toolchain file")
        elseif(EXISTS "/opt/vcpkg/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "/opt/vcpkg/scripts/buildsystems/vcpkg.cmake"
                CACHE STRING "Vcpkg toolchain file")
        endif()
    endif()
endif()

if(CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
else()
    message(STATUS "No vcpkg toolchain found - using system packages")
endif()
message(STATUS "Vcpkg triplet: ${VCPKG_TARGET_TRIPLET}")

# ============================================================
# 项目配置
# ============================================================
# macOS: 设置最低部署目标，必须在 project() 之前
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS deployment target")
endif()

project(VulkanPBR VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Engine RHI backend (compile-time selection)
#   -DVENGINE_RHI_BACKEND=vulkan  (default, all platforms)
#   -DVENGINE_RHI_BACKEND=dx12    (Windows only; DX12 requires no Vulkan device)
# ============================================================
set(VENGINE_RHI_BACKEND "vulkan" CACHE STRING
    "RHI backend used by the engine executable (vulkan|dx12)")
set_property(CACHE VENGINE_RHI_BACKEND PROPERTY STRINGS vulkan dx12)

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
    # 方法2: 手动指定 GLFW 路径
    message(STATUS "glfw3 not found via find_package, trying manual configuration...")
    
    # 用户可以设置 GLFW_DIR 环境变量或 CMake 变量
    if(DEFINED ENV{GLFW_DIR})
        set(GLFW_DIR $ENV{GLFW_DIR})
    endif()
    
    if(GLFW_DIR)
        set(GLFW_INCLUDE_DIR "${GLFW_DIR}/include")
        if(WIN32)
            set(GLFW_LIBRARY "${GLFW_DIR}/lib-vc2022/glfw3.lib")
            if(NOT EXISTS ${GLFW_LIBRARY})
                set(GLFW_LIBRARY "${GLFW_DIR}/lib-vc2019/glfw3.lib")
            endif()
        elseif(APPLE)
            set(GLFW_LIBRARY "${GLFW_DIR}/lib/libglfw3.a")
            if(NOT EXISTS ${GLFW_LIBRARY})
                set(GLFW_LIBRARY "${GLFW_DIR}/lib/libglfw.dylib")
            endif()
        else()
            set(GLFW_LIBRARY "${GLFW_DIR}/lib/libglfw3.a")
        endif()
        message(STATUS "Using GLFW from: ${GLFW_DIR}")
    elseif(APPLE)
        # macOS: 尝试 Homebrew 安装路径
        find_library(GLFW_LIBRARY glfw HINTS /opt/homebrew/lib /usr/local/lib)
        find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h HINTS /opt/homebrew/include /usr/local/include)
        if(GLFW_LIBRARY AND GLFW_INCLUDE_DIR)
            message(STATUS "Found GLFW via Homebrew: ${GLFW_LIBRARY}")
        else()
            message(FATAL_ERROR "GLFW not found! Please install GLFW: brew install glfw")
        endif()
    else()
        message(FATAL_ERROR "GLFW not found! Please install GLFW or set GLFW_DIR")
    endif()
endif()

# ============================================================
# EnTT 配置 (ECS库)
# ============================================================
find_package(EnTT CONFIG QUIET)
if(NOT EnTT_FOUND)
    # 尝试非 CONFIG 模式
    find_package(EnTT QUIET)
endif()
if(EnTT_FOUND)
    message(STATUS "Found EnTT")
else()
    # 尝试 Homebrew / pkg-config 路径
    find_path(ENTT_INCLUDE_DIR entt/entt.hpp
        HINTS /opt/homebrew/include /usr/local/include
    )
    if(ENTT_INCLUDE_DIR)
        message(STATUS "Found EnTT headers at: ${ENTT_INCLUDE_DIR}")
        # 创建一个 INTERFACE target 以兼容 target_link_libraries
        add_library(EnTT::EnTT INTERFACE IMPORTED)
        set_target_properties(EnTT::EnTT PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${ENTT_INCLUDE_DIR}"
        )
    else()
        message(FATAL_ERROR "EnTT not found! Please install EnTT:\n"
            "  vcpkg: vcpkg install entt\n"
            "  Homebrew: brew install entt\n"
            "  Or set ENTT_INCLUDE_DIR manually")
    endif()
endif()

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
# nlohmann-json 配置 (场景序列化, vcpkg: nlohmann-json)
# ============================================================
find_package(nlohmann_json CONFIG QUIET)

if(NOT nlohmann_json_FOUND)
    message(WARNING
        "nlohmann-json not found! Scene serialization will fail to build. "
        "Install it via: vcpkg install nlohmann-json:x64-windows")
else()
    message(STATUS "Found nlohmann-json: ${nlohmann_json_VERSION}")
endif()

# ============================================================
# DirectX-Headers 配置 (Microsoft 开源 DX12 头文件)
# ============================================================
if(WIN32)
    if(NOT TARGET DirectX-Headers AND NOT TARGET Microsoft::DirectX-Headers)
        add_subdirectory(src/third_party/DirectX-Headers)
    endif()
    message(STATUS "DirectX-Headers enabled for DX12 support")
endif()

# ============================================================
# Include directories
# ============================================================
include_directories(${Vulkan_INCLUDE_DIRS})
include_directories(src)
include_directories(src/Application)
include_directories(src/RHI)
include_directories(src/RHI/Vulkan)
include_directories(src/RHI/DX12)
include_directories(src/Core)
include_directories(src/renderer)
include_directories(src/renderer/passes)
include_directories(src/renderer/passes/ssao)
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

# RHI - DX12 后端 (仅 Windows)
if(WIN32)
    set(DX12_SOURCES
        src/RHI/DX12/DX12RHIDevice.cpp
        src/RHI/DX12/DX12RHIBuffer.cpp
        src/RHI/DX12/DX12RHICommandBuffer.cpp
        src/RHI/DX12/DX12RHITexture.cpp
        src/RHI/DX12/DX12RHIPipeline.cpp
        src/RHI/DX12/DX12RHIRenderPass.cpp
        src/RHI/DX12/DX12RHIFramebuffer.cpp
        src/RHI/DX12/DX12RHIDescriptor.cpp
        src/RHI/DX12/DX12RHISampler.cpp
        src/RHI/DX12/DX12RHISwapChain.cpp
        src/RHI/DX12/DX12RHIShader.cpp
    )

    set(DX12_HEADERS
        src/RHI/DX12/DX12RHIDevice.h
        src/RHI/DX12/DX12RHIBuffer.h
        src/RHI/DX12/DX12RHICommandBuffer.h
        src/RHI/DX12/DX12RHITexture.h
        src/RHI/DX12/DX12RHIPipeline.h
        src/RHI/DX12/DX12RHIRenderPass.h
        src/RHI/DX12/DX12RHIFramebuffer.h
        src/RHI/DX12/DX12RHIDescriptor.h
        src/RHI/DX12/DX12RHISampler.h
        src/RHI/DX12/DX12RHISwapChain.h
        src/RHI/DX12/DX12TypeConversions.h
        src/RHI/DX12/DX12RHIShader.h
    )
else()
    set(DX12_SOURCES "")
    set(DX12_HEADERS "")
endif()
# RHI - 渲染硬件接口层：RHI 抽象层 + Vulkan 后端
set(RHI_SOURCES
    src/RHI/RHI.cpp
    src/RHI/Vulkan/VulkanRHIDevice.cpp
    src/RHI/Vulkan/VulkanRHIBuffer.cpp
    src/RHI/Vulkan/VulkanRHITexture.cpp
    src/RHI/Vulkan/VulkanRHISampler.cpp
    src/RHI/Vulkan/VulkanRHIShader.cpp
    src/RHI/Vulkan/VulkanRHIDescriptor.cpp
    src/RHI/Vulkan/VulkanRHIPipeline.cpp
    src/RHI/Vulkan/VulkanRHIRenderPass.cpp
    src/RHI/Vulkan/VulkanRHICommandBuffer.cpp
    src/RHI/Vulkan/VulkanRHISwapChain.cpp
)

set(RHI_HEADERS
    src/RHI/RHI.h
    src/RHI/RHITypes.h
    src/RHI/RHIDevice.h
    src/RHI/RHIBuffer.h
    src/RHI/RHITexture.h
    src/RHI/RHISampler.h
    src/RHI/RHIShader.h
    src/RHI/RHIDescriptor.h
    src/RHI/RHIPipeline.h
    src/RHI/RHIRenderPass.h
    src/RHI/RHICommandBuffer.h
    src/RHI/RHISwapChain.h
    src/RHI/Vulkan/VulkanRHIDevice.h
    src/RHI/Vulkan/VulkanRHIBuffer.h
    src/RHI/Vulkan/VulkanRHITexture.h
    src/RHI/Vulkan/VulkanRHISampler.h
    src/RHI/Vulkan/VulkanRHIShader.h
    src/RHI/Vulkan/VulkanRHIDescriptor.h
    src/RHI/Vulkan/VulkanRHIPipeline.h
    src/RHI/Vulkan/VulkanRHIRenderPass.h
    src/RHI/Vulkan/VulkanRHICommandBuffer.h
    src/RHI/Vulkan/VulkanRHISwapChain.h
    src/RHI/Vulkan/VulkanTypeConversions.h
)

# Core - 核心工具层：与渲染无关的基础设施
# (Utils.h/.cpp 已删除 — Vulkan debug messenger 与 radian/degree 辅助均无调用者)
set(CORE_SOURCES
)

set(CORE_HEADERS
)

# Renderer Passes - 渲染通道
set(PASSES_SOURCES
    src/renderer/passes/GBufferPass.cpp
    src/renderer/passes/SSRPass.cpp
    src/renderer/passes/WaterPass.cpp
    src/renderer/passes/ForwardPass.cpp
    src/renderer/passes/LightingPass.cpp
    src/renderer/passes/TransparentPass.cpp
    src/renderer/passes/ComputePassBase.cpp
    src/renderer/passes/FrustumCullingPass.cpp
    src/renderer/passes/GPUDrivenRenderer.cpp
    src/renderer/passes/NaniteDebugPass.cpp
    src/renderer/passes/ClusterCullingPass.cpp
    src/renderer/passes/ssao/SSAOPass.cpp
)

set(PASSES_HEADERS
    src/renderer/passes/RenderPassBase.h
    src/renderer/passes/GBufferPass.h
    src/renderer/passes/SSRPass.h
    src/renderer/passes/WaterPass.h
    src/renderer/passes/ForwardPass.h
    src/renderer/passes/LightingPass.h
    src/renderer/passes/TransparentPass.h
    src/renderer/passes/ComputePassBase.h
    src/renderer/passes/FrustumCullingPass.h
    src/renderer/passes/GPUDrivenRenderer.h
    src/renderer/passes/NaniteDebugPass.h
    src/renderer/passes/ClusterCullingPass.h
    src/renderer/passes/ssao/SSAOPass.h
)

# Renderer - 渲染器
set(RENDERER_SOURCES
    src/renderer/SceneRenderer.cpp
)

set(RENDERER_HEADERS
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
    src/resources/GPUMesh.cpp
    src/resources/MeshManager.cpp
    src/resources/TextureManager.cpp
    src/resources/ModelImporter.cpp
)

set(RESOURCES_HEADERS
    src/resources/Mesh.h
    src/resources/MeshManager.h
    src/resources/TextureManager.h
    src/resources/RenderSystem.h
    src/resources/ModelImporter.h
    src/resources/AssetPath.h
)

# Scene - ECS 场景管理系统
set(SCENE_SOURCES
    src/scene/Scene.cpp
    src/scene/Entity.cpp
    src/scene/SceneManager.cpp
    src/scene/SelectionManager.cpp
    src/scene/SceneSerializer.cpp
)

set(SCENE_HEADERS
    src/scene/Scene.h
    src/scene/Entity.h
    src/scene/SceneManager.h
    src/scene/Components.h
    src/scene/SelectionManager.h
    src/scene/RayPicker.h
    src/scene/SceneSerializer.h
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
    src/third_party/tiny_gltf.h
)

# Third Party Sources - tinygltf 实现 + NFD 原生文件对话框
set(THIRD_PARTY_SOURCES
    src/third_party/tiny_gltf_impl.cpp
)

if(WIN32)
    list(APPEND THIRD_PARTY_SOURCES src/third_party/nfd/nfd_win.cpp)
    list(APPEND THIRD_PARTY_HEADERS src/third_party/nfd/nfd.h)
elseif(APPLE)
    list(APPEND THIRD_PARTY_SOURCES src/third_party/nfd/nfd_cocoa.m)
    list(APPEND THIRD_PARTY_HEADERS src/third_party/nfd/nfd.h)
endif()

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

if(WIN32)
    # imgui_impl_dx12 仅供 ImGuiLayer 的 DX12 分支使用（Windows 专用）
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_dx12.cpp)
endif()

set(IMGUI_HEADERS
    ${IMGUI_DIR}/imgui.h
    ${IMGUI_DIR}/imgui_internal.h
    ${IMGUI_DIR}/imconfig.h
    ${IMGUI_DIR}/imstb_rectpack.h
    ${IMGUI_DIR}/imstb_textedit.h
    ${IMGUI_DIR}/imstb_truetype.h
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.h
    ${IMGUI_DIR}/backends/imgui_impl_glfw.h
    ${IMGUI_DIR}/backends/imgui_impl_dx12.h
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
    ${DX12_SOURCES}
    ${CORE_SOURCES}
    ${PASSES_SOURCES}
    ${RENDERER_SOURCES}
    ${WORLD_SOURCES}
    ${RESOURCES_SOURCES}
    ${SCENE_SOURCES}
    ${NANITE_SOURCES}
    ${IMGUI_SOURCES}
    ${THIRD_PARTY_SOURCES}
    ${UI_SOURCES}
)

set(ALL_HEADERS
    ${APPLICATION_HEADERS}
    ${RHI_HEADERS}
    ${DX12_HEADERS}
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
source_group("Source Files/RHI/DX12" FILES ${DX12_SOURCES})
source_group("Source Files/Core" FILES ${CORE_SOURCES})
source_group("Source Files/Renderer" FILES ${RENDERER_SOURCES})
source_group("Source Files/Renderer/Passes" FILES ${PASSES_SOURCES})
source_group("Source Files/World" FILES ${WORLD_SOURCES})
source_group("Source Files/Resources" FILES ${RESOURCES_SOURCES})
source_group("Source Files/Scene" FILES ${SCENE_SOURCES})
source_group("Header Files/Nanite" FILES ${NANITE_HEADERS})
source_group("Source Files/Nanite" FILES ${NANITE_SOURCES})
source_group("Source Files/Third Party" FILES ${THIRD_PARTY_SOURCES})
source_group("Source Files/ImGui" FILES ${IMGUI_SOURCES})
source_group("Source Files/UI" FILES ${UI_SOURCES})

source_group("Header Files/Application" FILES ${APPLICATION_HEADERS})
source_group("Header Files/RHI" FILES ${RHI_HEADERS})
source_group("Header Files/RHI/DX12" FILES ${DX12_HEADERS})
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

if(nlohmann_json_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
endif()

# tinygltf: 引擎只提取贴图 URI 路径, 不解码像素 (stb_image 由 TextureManager 单独使用)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    TINYGLTF_NO_STB_IMAGE
    TINYGLTF_NO_STB_IMAGE_WRITE
)

# ============================================================
# Engine RHI backend compile definition
# ============================================================
if(VENGINE_RHI_BACKEND STREQUAL "dx12")
    if(NOT WIN32)
        message(FATAL_ERROR "VENGINE_RHI_BACKEND=dx12 is only supported on Windows")
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE VENGINE_RHI_DX12=1)
    message(STATUS "Engine RHI backend: DX12 (VENGINE_RHI_DX12)")
else()
    message(STATUS "Engine RHI backend: Vulkan")
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

    # DirectX 12 支持 (通过 DirectX-Headers 开源项目)
    target_link_libraries(${PROJECT_NAME} PRIVATE Microsoft::DirectX-Headers Microsoft::DirectX-Guids)
    target_link_libraries(${PROJECT_NAME} PRIVATE d3d12 dxgi dxguid)
    target_link_libraries(${PROJECT_NAME} PRIVATE d3dcompiler)  # D3DReadFileToBlob (DX12RHIShader)
    
    # Visual Studio 特定设置
    if(MSVC)
        # 设置启动项目
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
        
        # 设置调试工作目录
        set_target_properties(${PROJECT_NAME} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        )
        
        # 编译选项 (/utf-8: 源码为 UTF-8;否则 MSVC 按代码页 936 读中文注释可能
        # 把多字节序列误读为 '\' 续行符,破坏代码结构)
        target_compile_options(${PROJECT_NAME} PRIVATE /W4 /MP /utf-8)
        
        # 使用 Unicode
        target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)
    endif()
    
elseif(APPLE)
    # macOS 平台配置 (MoltenVK / Vulkan SDK for macOS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VK_USE_PLATFORM_METAL_EXT
        GLFW_INCLUDE_VULKAN
    )
    
    # 编译选项
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wno-unused-function -Wno-missing-braces)
    
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreGraphics"
        "-framework CoreFoundation"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework AppKit"
    )
    
elseif(UNIX)
    # Linux 平台配置
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VK_USE_PLATFORM_XCB_KHR
        GLFW_INCLUDE_VULKAN
    )
endif()

# ============================================================
# DX12 冒烟 demo target (仅 Windows): RHI 全链路纹理三角形
# ============================================================
# Phase 1 验收: 2-frame 主循环 + 描述符链 + push constants + 读回 + 验证层自检。
# shader 策略: GLSL -> glslc(.spv) -> spirv-cross(--hlsl sm60) -> 注入 push
# constant register -> dxc(.dxil); 输出到 bin/shaders_dx12。
if(WIN32)
    # ---- DX12 shader 内容管线工具 ----
    find_program(DX12_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin")
    find_program(DX12_SPIRV_CROSS spirv-cross HINTS "$ENV{VULKAN_SDK}/Bin")
    find_program(DX12_DXC dxc HINTS "$ENV{VULKAN_SDK}/Bin")

    if(DX12_GLSLC AND DX12_SPIRV_CROSS AND DX12_DXC)
        message(STATUS "DX12 shader chain: ${DX12_GLSLC} + ${DX12_SPIRV_CROSS} + ${DX12_DXC}")

        set(DX12_SMOKE_GLSL_DIR "${CMAKE_SOURCE_DIR}/src/smoke/dx12_smoke/shaders")
        set(DX12_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders_dx12")
        set(DX12_SHADER_CACHE_DIR "${CMAKE_BINARY_DIR}/shaders_dx12_intermediate")
        file(MAKE_DIRECTORY ${DX12_SHADER_OUT_DIR})
        file(MAKE_DIRECTORY ${DX12_SHADER_OUT_DIR}/ssao)
        file(MAKE_DIRECTORY ${DX12_SHADER_OUT_DIR}/culling)
        file(MAKE_DIRECTORY ${DX12_SHADER_OUT_DIR}/nanite)
        file(MAKE_DIRECTORY ${DX12_SHADER_CACHE_DIR})

        # ---- 通用单 shader 编译链 ----
        # GLSL -> glslc(.spv) -> spirv-cross(--hlsl sm60) -> inject_hlsl.ps1
        # (push-constant cbuffer 注入 register(b0, space=N)) -> dxc(.dxil)
        # 产物名与引擎运行时约定一致: pass 硬编码 "shaders/X.spv", DX12RHIShader
        # 加载时转译为 "shaders_dx12/X.dxil"。
        # 参数: <GLSL 全路径> <vs|ps|cs> <pushConstantSpace=所属管线 layout 数>
        #       <dxil 相对名> <flipY: 0|1> <输出列表变量名>
        # flipY=1 时给 spirv-cross 加 --flip-vert-y: 全屏 pass 的顶点着色器按
        # Vulkan NDC 约定生成 (uv.y=0 -> NDC y=-1), 而 D3D 的 NDC y=-1 在屏幕
        # 底部, 需翻转才能与 Vulkan 采样方向一致 (仅影响 DX12 的 HLSL/DXIL 链)。
        function(dx12_compile_shader DX12F_GLSL DX12F_STAGE DX12F_PC_SPACE DX12F_DXIL_REL DX12F_FLIP_Y DX12F_OUTVAR)
            # Cache intermediates are named after the ASCII dxil output (paths
            # may contain non-ASCII repo segments, which some tools mishandle).
            string(REPLACE "/" "_" DX12F_STEM ${DX12F_DXIL_REL})
            string(REPLACE "." "_" DX12F_STEM ${DX12F_STEM})
            set(DX12F_SPV   "${DX12_SHADER_CACHE_DIR}/${DX12F_STEM}.spv")
            set(DX12F_HLSL  "${DX12_SHADER_CACHE_DIR}/${DX12F_STEM}.hlsl")
            set(DX12F_DXIL  "${DX12_SHADER_OUT_DIR}/${DX12F_DXIL_REL}")

            if(DX12F_STAGE STREQUAL "vs")
                set(DX12F_PROFILE "vs_6_0")
            elseif(DX12F_STAGE STREQUAL "cs")
                set(DX12F_PROFILE "cs_6_0")
            else()
                set(DX12F_PROFILE "ps_6_0")
            endif()

            set(DX12F_CROSS_ARGS --hlsl --shader-model 60)
            if(DX12F_FLIP_Y)
                list(APPEND DX12F_CROSS_ARGS --flip-vert-y)
            endif()

            add_custom_command(
                OUTPUT ${DX12F_DXIL}
                COMMAND ${DX12_GLSLC} ${DX12F_GLSL} -o ${DX12F_SPV}
                COMMAND ${DX12_SPIRV_CROSS} ${DX12F_SPV} ${DX12F_CROSS_ARGS} --output ${DX12F_HLSL}
                COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
                    "${CMAKE_SOURCE_DIR}/scripts/dx12/inject_hlsl.ps1"
                    -InFile ${DX12F_HLSL} -OutFile ${DX12F_HLSL}
                    -PushConstantSpace ${DX12F_PC_SPACE}
                COMMAND ${DX12_DXC} -T ${DX12F_PROFILE} -E main -Fo ${DX12F_DXIL} ${DX12F_HLSL}
                DEPENDS ${DX12F_GLSL} "${CMAKE_SOURCE_DIR}/scripts/dx12/inject_hlsl.ps1"
                COMMENT "DX12 shader: ${DX12F_GLSL} -> shaders_dx12/${DX12F_DXIL_REL} (space=${DX12F_PC_SPACE}, flipY=${DX12F_FLIP_Y})"
            )
            set(${DX12F_OUTVAR} ${${DX12F_OUTVAR}} ${DX12F_DXIL} PARENT_SCOPE)
        endfunction()

        # ---- DX12 smoke shader 集 (demo + 设备内置 blit/clear 管线) ----
        # <glsl 名>|<stage>|<pushConstantSpace>|<dxil 相对名>|<flipY: 0|1>
        # blit 是全屏三角形, 需 flipY 才能在 D3D 下与 Vulkan 采样方向一致。
        set(DX12_SMOKE_SHADERS
            demo.vert|vs|2|demo_vert.dxil|0
            demo.frag|ps|2|demo_frag.dxil|0
            blit.vert|vs|0|blit_vert.dxil|1
            blit.frag|ps|0|blit_frag.dxil|0
            clear.vert|vs|0|clear_vert.dxil|0
            clear.frag|ps|0|clear_frag.dxil|0
        )
        set(DX12_SMOKE_OUTPUTS)
        foreach(SHADER_SPEC ${DX12_SMOKE_SHADERS})
            string(REPLACE "|" ";" SHADER_PARTS ${SHADER_SPEC})
            list(GET SHADER_PARTS 0 SHADER_SRC)
            list(GET SHADER_PARTS 1 SHADER_STAGE)
            list(GET SHADER_PARTS 2 SHADER_PC_SPACE)
            list(GET SHADER_PARTS 3 SHADER_DXIL_NAME)
            list(GET SHADER_PARTS 4 SHADER_FLIP_Y)
            dx12_compile_shader("${DX12_SMOKE_GLSL_DIR}/${SHADER_SRC}"
                                ${SHADER_STAGE} ${SHADER_PC_SPACE} ${SHADER_DXIL_NAME} ${SHADER_FLIP_Y} DX12_SMOKE_OUTPUTS)
        endforeach()

        add_custom_target(CompileDX12Shaders ALL DEPENDS ${DX12_SMOKE_OUTPUTS})

        # ---- 引擎 shader 集 (Phase 4: deferred/SSAO/culling/Nanite 全量) ----
        # pushConstantSpace = 该 shader 所属管线的 binding layout 数 (见 DX12-RHI-Notes §11)
        # dxil 相对名须与 DX12RHIShader 的运行时转译一致:
        #   "shaders/<rel>.spv" -> "shaders_dx12/<rel>.dxil"
        # 末列 flipY=1: 全屏 pass 顶点着色器 (Vulkan NDC 约定, D3D 需翻转 Y)
        set(DX12_ENGINE_SHADERS
            pbr.vert|vs|2|pbr_vert.dxil|0
            pbr.frag|ps|2|pbr_frag.dxil|0
            transparent.frag|ps|2|transparent_frag.dxil|0
            gbuffer.vert|vs|2|gbuffer_vert.dxil|0
            gbuffer.frag|ps|2|gbuffer_frag.dxil|0
            deferred_lighting.vert|vs|1|deferred_lighting_vert.dxil|1
            deferred_lighting.frag|ps|1|deferred_lighting_frag.dxil|0
            water.vert|vs|1|water_vert.dxil|0
            water.frag|ps|1|water_frag.dxil|0
            ssr.vert|vs|1|ssr_vert.dxil|1
            ssr.frag|ps|1|ssr_frag.dxil|0
            ssao/ssao.vert|vs|1|ssao/ssao_vert.dxil|1
            ssao/ssao.frag|ps|1|ssao/ssao_frag.dxil|0
            ssao/ssao_blur.vert|vs|1|ssao/ssao_blur_vert.dxil|1
            ssao/ssao_blur.frag|ps|1|ssao/ssao_blur_frag.dxil|0
            ssao/ssao_deinterleave.comp|cs|1|ssao/ssao_deinterleave_comp.dxil|0
            ssao/ssao_reinterleave.comp|cs|1|ssao/ssao_reinterleave_comp.dxil|0
            culling/frustum_culling.comp|cs|1|culling/frustum_culling.comp.dxil|0
            nanite/cluster_culling.comp|cs|1|nanite/cluster_culling.comp.dxil|0
            nanite/build_visible_geometry.comp|cs|1|nanite/build_visible_geometry.comp.dxil|0
            nanite/cluster_debug.vert|vs|1|nanite/cluster_debug_vert.dxil|0
            nanite/cluster_debug.frag|ps|1|nanite/cluster_debug_frag.dxil|0
        )
        set(DX12_ENGINE_OUTPUTS)
        foreach(SHADER_SPEC ${DX12_ENGINE_SHADERS})
            string(REPLACE "|" ";" SHADER_PARTS ${SHADER_SPEC})
            list(GET SHADER_PARTS 0 SHADER_SRC)
            list(GET SHADER_PARTS 1 SHADER_STAGE)
            list(GET SHADER_PARTS 2 SHADER_PC_SPACE)
            list(GET SHADER_PARTS 3 SHADER_DXIL_NAME)
            list(GET SHADER_PARTS 4 SHADER_FLIP_Y)
            dx12_compile_shader("${CMAKE_SOURCE_DIR}/shaders/${SHADER_SRC}"
                                ${SHADER_STAGE} ${SHADER_PC_SPACE} ${SHADER_DXIL_NAME} ${SHADER_FLIP_Y} DX12_ENGINE_OUTPUTS)
        endforeach()

        add_custom_target(CompileDX12EngineShaders ALL DEPENDS ${DX12_ENGINE_OUTPUTS})
        # The engine's internal blit/clear pipelines (blitImage/clearColorImage)
        # load shaders_dx12/blit_*.dxil / clear_*.dxil, which are compiled from
        # the smoke shader set — the engine target must build those too.
        add_dependencies(${PROJECT_NAME} CompileDX12EngineShaders CompileDX12Shaders)
    else()
        message(WARNING "DX12 shader tools (glslc/spirv-cross/dxc) not found; skipping DX12 shader compilation")
    endif()

    # ---- demo executable: DX12 RHI 后端 + 冒烟 demo ----
    add_executable(rhi_dx12_smoke src/smoke/dx12_smoke/main.cpp ${DX12_SOURCES})

    target_compile_definitions(rhi_dx12_smoke PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )

    # DirectX-Headers 子模块 + Windows 平台库
    target_link_libraries(rhi_dx12_smoke PRIVATE Microsoft::DirectX-Headers Microsoft::DirectX-Guids)
    target_link_libraries(rhi_dx12_smoke PRIVATE d3d12 dxgi dxguid d3dcompiler)

    if(glfw3_FOUND)
        target_link_libraries(rhi_dx12_smoke PRIVATE glfw)
    elseif(DEFINED GLFW_LIBRARY)
        target_link_libraries(rhi_dx12_smoke PRIVATE ${GLFW_LIBRARY})
    endif()

    if(MSVC)
        target_compile_options(rhi_dx12_smoke PRIVATE /W4 /MP /utf-8)
        set_target_properties(rhi_dx12_smoke PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        )
    endif()

    source_group("Source Files/Smoke" FILES src/smoke/dx12_smoke/main.cpp)

    if(TARGET CompileDX12Shaders)
        add_dependencies(rhi_dx12_smoke CompileDX12Shaders)
    endif()

    # 运行时依赖: 将 vcpkg 的 glfw DLL 拷贝到冒烟目标旁
    if(TARGET glfw)
        add_custom_command(TARGET rhi_dx12_smoke POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:glfw>"
                "$<TARGET_FILE_DIR:rhi_dx12_smoke>/$<TARGET_FILE_NAME:glfw>"
            COMMENT "Copying glfw DLL next to rhi_dx12_smoke"
        )
    endif()
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
find_program(GLSLC glslc HINTS
    "$ENV{VULKAN_SDK}/bin"          # macOS/Linux (lowercase bin)
    "$ENV{VULKAN_SDK}/Bin"          # Windows (uppercase Bin)
    "/opt/homebrew/bin"             # macOS Homebrew
    "/usr/local/bin"                # macOS/Linux common
)

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
        transparent.frag
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
    
    # SSAO 着色器（带子目录）
    set(SSAO_SHADER_SOURCES
        ssao/ssao_deinterleave.comp
        ssao/ssao.vert
        ssao/ssao.frag
        ssao/ssao_reinterleave.comp
        ssao/ssao_blur.vert
        ssao/ssao_blur.frag
    )

    # 计算着色器（带子目录）
    set(COMPUTE_SHADER_SOURCES
        culling/frustum_culling.comp
        nanite/cluster_culling.comp
        nanite/build_visible_geometry.comp
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
    
    # 编译 SSAO 着色器（保持 ssao/ 子目录结构）
    foreach(SHADER_FILE ${SSAO_SHADER_SOURCES})
        set(SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE}")

        get_filename_component(SHADER_SUBDIR ${SHADER_FILE} DIRECTORY)
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)

        if(SHADER_SUBDIR)
            file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}/${SHADER_SUBDIR}")
        endif()

        string(REPLACE "." "_" SHADER_OUTPUT_NAME ${SHADER_NAME})
        set(SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${SHADER_SUBDIR}/${SHADER_OUTPUT_NAME}.spv")

        if(EXISTS ${SHADER_SOURCE})
            set(SHADER_SOURCE_DIR_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/${SHADER_SUBDIR}/${SHADER_OUTPUT_NAME}.spv")
            add_custom_command(
                OUTPUT ${SHADER_OUTPUT}
                COMMAND ${GLSLC} ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
                COMMAND ${CMAKE_COMMAND} -E copy ${SHADER_OUTPUT} ${SHADER_SOURCE_DIR_OUTPUT}
                DEPENDS ${SHADER_SOURCE}
                COMMENT "Compiling SSAO shader: ${SHADER_FILE} -> ${SHADER_OUTPUT_NAME}.spv"
            )

            list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
            message(STATUS "  SSAO Shader: ${SHADER_FILE} -> ${SHADER_OUTPUT_NAME}.spv")
        else()
            message(WARNING "SSAO shader file not found: ${SHADER_SOURCE}")
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

    # 编译 Nanite 图形着色器 (cluster_debug; 保持 nanite/ 子目录结构, dot->underscore)
    set(NANITE_SHADER_SOURCES
        nanite/cluster_debug.vert
        nanite/cluster_debug.frag
    )
    foreach(SHADER_FILE ${NANITE_SHADER_SOURCES})
        set(SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE}")

        get_filename_component(SHADER_SUBDIR ${SHADER_FILE} DIRECTORY)
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)

        if(SHADER_SUBDIR)
            file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}/${SHADER_SUBDIR}")
        endif()

        string(REPLACE "." "_" SHADER_OUTPUT_NAME ${SHADER_NAME})
        set(SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${SHADER_SUBDIR}/${SHADER_OUTPUT_NAME}.spv")

        if(EXISTS ${SHADER_SOURCE})
            set(SHADER_SOURCE_DIR_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/${SHADER_SUBDIR}/${SHADER_OUTPUT_NAME}.spv")
            add_custom_command(
                OUTPUT ${SHADER_OUTPUT}
                COMMAND ${GLSLC} ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
                COMMAND ${CMAKE_COMMAND} -E copy ${SHADER_OUTPUT} ${SHADER_SOURCE_DIR_OUTPUT}
                DEPENDS ${SHADER_SOURCE}
                COMMENT "Compiling Nanite shader: ${SHADER_FILE} -> ${SHADER_OUTPUT_NAME}.spv"
            )

            list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
            message(STATUS "  Nanite Shader: ${SHADER_FILE} -> ${SHADER_OUTPUT_NAME}.spv")
        else()
            message(WARNING "Nanite shader file not found: ${SHADER_SOURCE}")
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
message(STATUS "  src/renderer/    - SceneRenderer")
message(STATUS "  src/renderer/passes/ - Render passes (GBuffer, SSR, SSAO, etc.)")
message(STATUS "  src/World/       - Camera, Scene objects")
message(STATUS "  src/resources/   - Mesh, Material")
message(STATUS "  src/scene/       - ECS scene management")
message(STATUS "  src/renderer/nanite/ - Virtual geometry system")
message(STATUS "  src/ui/          - Editor UI panels")
message(STATUS "===============================")
message(STATUS "")