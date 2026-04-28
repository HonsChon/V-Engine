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

## 5. `static const` 整型成员被 ODR-use 时链接报错

**现象**：类内声明 `static const int MAX_FRAMES_IN_FLIGHT = 2;`，编译通过，但链接报 `Undefined symbol: SceneRenderer::MAX_FRAMES_IN_FLIGHT`。

**原因**：C++ 标准中 `static const` 整型成员在类内给了初始值只是"声明"，如果它被"ODR-use"（取地址、传引用、作为函数实参需要隐式取地址等），链接器就需要在某个 `.cpp` 中找到它的**定义**（即独立的存储）。常见触发场景：把它传给接收 `const int&` 参数的函数（比如 `std::make_unique` 的构造参数转发）。

C++17 引入了 **`inline` 变量**，`static constexpr` 成员自动成为 `inline`，不再需要类外定义。

### 深入理解：为什么是 Undefined 而不是 Duplicate？

很多人会疑惑：头文件被多个 `.cpp` include，不是每个 `.cpp` 都有一份吗，为啥不是"重复定义"而是"找不到"？

关键在于 **类内的 `static` 和文件作用域的 `static` 是完全不同的东西**：

| 写法 | `static` 的含义 | 链接性 | 结果 |
|------|----------------|--------|------|
| 文件作用域 `static const int X = 2;` | **内部链接** | 每个 .cpp 各有独立的一份 | 不冲突，也不会丢 |
| 类内 `static const int X = 2;` | **属于类，不属于实例** | **外部链接，全局唯一** | 只是声明，没有定义 → Undefined |
| 类内 `static constexpr int X = 2;` | 属于类 | 外部链接 + C++17 自动 inline | 自带定义，OK |

类内 `static` 成员具有**外部链接**——全程序只应有一份实体。所有 `.cpp` 看到的是同一个声明，但没有任何一个 `.cpp` 真正"创建"了它的存储：

```
SceneRenderer.cpp  →  看到声明，知道有这个变量
VulkanRenderer.cpp →  看到声明，知道有这个变量
链接器 → 谁提供这个变量的实体？→ 没人提供！→ Undefined symbol ❌
```

### "自动 inline" 是什么意思？

这里的 `inline` 不是"内联展开函数"，而是 C++17 的**链接属性**：允许同一个定义出现在多个编译单元中，链接器只保留一份，不报重复定义错误。

`static constexpr` 在 C++17 中自动带 `inline`，等价于：
```cpp
class SceneRenderer {
    static inline constexpr int MAX_FRAMES_IN_FLIGHT = 2;  // 头文件里写了就是定义
};
```
不管被多少个 `.cpp` include，链接器帮你合并成一份。

```cpp
// ❌ 链接报 Undefined symbol
class SceneRenderer {
    static const int MAX_FRAMES_IN_FLIGHT = 2;  // 只是声明
};
// 被 ODR-use：传给接收 const uint32_t& 的函数
m_forwardPass = std::make_unique<ForwardPass>(..., MAX_FRAMES_IN_FLIGHT);

// ✅ 修复方案 A：改用 constexpr（推荐，C++17）
class SceneRenderer {
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;  // 自动 inline，无需类外定义
};

// ✅ 修复方案 B：在 .cpp 中补类外定义（C++11/14 兼容）
// SceneRenderer.cpp
const int SceneRenderer::MAX_FRAMES_IN_FLIGHT;  // 定义（值已在类内给出）
```

**解法**：优先用 `static constexpr`（C++17 自动 inline）。如果需要兼容 C++14，就在 `.cpp` 文件中加一行类外定义。

**注意**：MSVC 通常不报这个错，因为它对 ODR-use 的处理更宽松；Clang/GCC 严格按标准来。

---

## 速查清单

| 检查项 | 要点 |
|--------|------|
| 嵌套 struct 默认参数 | 移到类外定义 |
| 标准库 include | 用啥 include 啥 |
| 纹理/资源尺寸 | macOS Metal 上限 16384 |
| 部署目标 | 手动设 `CMAKE_OSX_DEPLOYMENT_TARGET` |
| GPU feature（如各向异性） | 运行时查询，别硬编码 `VK_TRUE` |
| `static const` 整型成员 | 改 `constexpr`，或在 `.cpp` 加类外定义 |
