# Sponza (Khronos glTF 版)

经典 GI/光追测试场景 — 杜布罗夫尼克斯蓬扎宫中庭（Atrium Sponza Palace）。

- 原始模型: Marko Dabrovic (2002), Frank Meinl / Crytek 重制版 (2010)，捐赠给公众用于
  辐射度/全局光照研究
- PBR 贴图: Alexandre Pestana (alexandre-pestana.com) Sponza PBR 贴图包
- glTF 版本: KhronosGroup/glTF-Sample-Models
  https://github.com/KhronosGroup/glTF-Sample-Models/tree/main/2.0/Sponza
  (MikkTSpace 切线已预计算；metallicRoughness 按 glTF 布局打包 G=roughness/B=metallic；
  贴图为 PNG/JPG；mesh 已经过 meshopt 优化)

## 使用

导入: 资源浏览器双击 `Sponza.gltf`，或拖拽到窗口，或

```
VulkanPBR --import ../../assets/Sponza/Sponza.gltf
```

示例场景: `assets/scenes/sponza.vscene`（含 Directional 平行光模拟中庭阳光）。

## 许可

见 `SOURCE_README.md`（Khronos 仓库原 README，含完整版权与来源说明）。
模型与贴图捐赠给公众用于渲染研究，保留原版权声明即可自由使用。

## 已知引擎限制

- metallicRoughness 打包贴图近似映射为 metallicMap（G/B 通道未拆分）
- alpha 贴图（旗帜/植被遮罩）按不透明渲染
- doubleSided 材质按单面渲染
