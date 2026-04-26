# C++ 踩坑记录

> V-Engine 从 Windows (MSVC) 移植到 macOS (AppleClang) 过程中踩过的坑。  
> 核心原因：**MSVC 宽松，Clang 严格**。

---

## 1. 嵌套结构体的默认成员初始化器

**现象**：类内部定义了一个带默认值的嵌套 `struct Config`，然后把 `Config{}` 作为构造函数默认参数，MSVC 通过，Clang 报错。

**原因**：C++ 标准规定嵌套类型的默认成员初始化器属于外围类的"完整类上下文"，要等外围类的 `}` 之后才生效。Clang 严格执行，MSVC 延迟解析所以放过了。

```cpp
// ❌ Clang 报错
class Engine {
    struct Config {
        std::string title = "Hello";  // 默认成员初始化器
    };
    Engine(const Config& c = Config{});  // Config{} 需要上面的初始化器，但 Engine 还没定义完
};

// ✅ 修复：struct 移到类外
struct EngineConfig {
    std::string title = "Hello";
};
class Engine {
    using Config = EngineConfig;
    Engine(const Config& c = Config{});  // 现在 OK
};
```

**解法**：把嵌套 struct 移到类外面定义，类内用 `using` 保持接口不变。

---

## 2. 缺少显式 `#include`

**现象**：用了 `std::vector` 但只 include 了 `<memory>`，MSVC 通过，Clang 报 "No template named 'vector'"。

**原因**：MSVC 的 `<memory>` 会间接带入 `<vector>`。Clang/libc++ 不会，每个头文件只管自己的。

```cpp
// ❌ MSVC 侥幸通过，Clang 报错
#include <memory>
std::vector<int> v;  // vector 没有被 include

// ✅ 显式 include
#include <memory>
#include <vector>
std::vector<int> v;
```

**解法**：用了 `std::xxx` 就 `#include <xxx>`，不依赖隐式引入。

---

## 3. macOS Metal 最大纹理尺寸 16384

**现象**：一张 21600×10800 的纹理在 Windows 正常加载，macOS 上 Metal 直接 assert 崩溃。

**原因**：macOS 的 Vulkan 走 MoltenVK → Metal，Metal 最大 2D 纹理尺寸 16384。Windows 上 N/A 卡通常支持 32768。

```cpp
// ❌ 直接创建超大纹理，macOS 崩溃
createImage(21600, 10800, ...);

// ✅ macOS 下检测并缩放
#ifdef __APPLE__
if (width > 16384 || height > 16384) {
    // 等比缩小到 16384 以内
}
#endif
```

**解法**：纹理加载时检测尺寸，超限则自动等比缩放。用 `#ifdef __APPLE__` 限定只在 macOS 生效。

---

## 4. Xcode 默认部署目标 = 自身版本号

**现象**：Xcode 26.2 生成的项目要求 macOS 26.2，但开发机是 macOS 15.7.3，拒绝运行。

**原因**：Xcode 默认把 minimum deployment target 设成与自身版本号一致。

```cmake
# ❌ 没设置，Xcode 默认用 26.2
project(VulkanPBR VERSION 1.0.0)

# ✅ project() 之前手动指定
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")
project(VulkanPBR VERSION 1.0.0)
```

**解法**：CMakeLists.txt 的 `project()` 之前设置部署目标。改完必须删 build 目录重新 configure。

---

## 速查清单

| 检查项 | 要点 |
|--------|------|
| 嵌套 struct 默认参数 | 移到类外定义 |
| 标准库 include | 用啥 include 啥 |
| 纹理/资源尺寸 | macOS Metal 上限 16384 |
| 部署目标 | 手动设 `CMAKE_OSX_DEPLOYMENT_TARGET` |
| GPU feature（如各向异性） | 运行时查询，别硬编码 `VK_TRUE` |