# Custom Material-Graph Nodes (BXDF / helper functions)

[English](../material_custom_nodes.md) | **简体中文**

项目可以通过在项目下的任意位置放置一个 `*.vmatnode.json`
描述符来添加自定义材质图（material-graph）节点。它们会被扫描并叠加到
内置节点集之上（`loadProjectMaterialGraphNodes` → `makeBuiltinNodeRegistry()`），
从而出现在材质图编辑器中，并被编译器使用。

## 表面输出节点（按 shading model）

一个表面图（surface graph）恰好以**一个**按模型的输出节点结束 —— shading model 即
节点身份（`typeId`），而非一个参数，因此每个节点只暴露该模型
实际使用的引脚（pin）：

| 输出节点 `typeId`    | Shading model            | 除 baseColor/normal/emissive/alpha/alphaCutoff 之外的额外引脚 |
|-------------------------|--------------------------|---------------------------------------------------------------|
| `vultra.output.pbr_mr`  | PBR Metallic-Roughness   | `metallic`、`roughness`、`ao`                                 |
| `vultra.output.pbr_sg`  | PBR Specular-Glossiness  | `specular`、`glossiness`、`ao`                                 |
| `vultra.output.phong`   | Phong                    | `specular`、`shininess`、`ao`                                 |
| `vultra.output.unlit`   | Unlit                    | （无 —— 仅 baseColor/alpha）                                 |
| `vultra.output.toon`    | Toon / cel               | `ao`                                                          |
| `vultra.output.custom`  | 已注册的自定义模型| `shadingModelName` 参数（根据注册表解析）      |

每个节点会把其 GBuffer 模型代码烘焙进生成的表面（`surface.shadingModel`）；
GBuffer 是 metallic-roughness 形态的，因此 SG/Phong 会在 codegen 阶段被转换为它，
与手工编写的资产路径（`thin_gbuffer.frag` `material_mra`）保持一致。使用旧的单一
`vultra.output.surface` 节点（+ 一个 `shadingModel` 参数）编写的旧图，会在加载时
迁移到匹配的按模型节点（`graphFromJson`）。

“什么算作输出节点”的唯一真理来源是
`material_graph::surfaceOutputTypeIds()` / `isSurfaceOutputType()`，由
验证器、编译器、渲染系统和编辑器共享。

## 按名称引用一个 helper / BXDF 函数

自定义节点的 `implementation.outputs` 可以调用一个经由 `implementation.includes`
从 shader-library 工件中引入的**完整 GLSL 函数**，而不必
内联这些数学运算。这正是用户自定义 BXDF/shading helper 的基础。

示例 —— [`resources/materials/nodes/sheen.vmatnode.json`](../../resources/materials/nodes/sheen.vmatnode.json)
引用 [`resources/shaders/bxdf/sheen.glsl`](../../resources/shaders/bxdf/sheen.glsl)：

```json
{
  "type": "MaterialGraphNode", "version": 1,
  "typeId": "project.bxdf.sheen", "displayName": "Sheen Rim",
  "inputs": [
    { "name": "tint", "type": "color", "default": [1,1,1,1] },
    { "name": "normalWS", "type": "vec3" }, { "name": "viewDirWS", "type": "vec3" },
    { "name": "intensity", "type": "float", "default": 1.0 }
  ],
  "outputs": [ { "name": "rgb", "type": "vec3" } ],
  "implementation": {
    "language": "glsl",
    "includes": [ "bxdf/sheen.glsl" ],
    "outputs": {
      "rgb": "vultra_node_sheen({{input:tint}}.rgb, {{input:normalWS}}, {{input:viewDirWS}}, {{input:intensity}})"
    }
  }
}
```

编译器会收集每个被使用节点的 `implementation.includes`，并在生成的表面源码顶部
发出 `#include` 指令，使节点的表达式得以调用被 include 的函数。`{{input:name}}` / `{{param:name}}`
照旧被替换。由 `tests/material_graph` 验证（断言生成源码中出现了
`#include` 以及该调用）。

shading model 在设计上与路径无关（`builtin/shaders/include/vultra/shading_model_abi.glsl`）：
一个写成 `vec3 fn(VultraSurface, VultraLight, VultraShadingExtra)` 的 BXDF
可以通过生成的 `vultra_eval_bxdf(model, …)` 分派从任意 lighting 路径
调用 —— 今天是 deferred，之后是 forward+。

## 运行时注意事项（重要）

两个引擎现实情况界定了**今天**能渲染什么，以及还需要哪些进一步工作：

1. **可常量折叠（constant-foldable）的图会被打包为真实的按模型材质。** 当一个
   图的表面归约为常量时，`render_system.cpp::packGraphConstantMaterial`
   会打包匹配的 `MaterialParamsPBRMR/PBRSG/Phong/Unlit/Toon`
   块并设置 GpuMaterial 的真实模型代码 —— 它通过与手工编写的 `.vmat.json`
   材质**完全相同的 GPU 路径**渲染（不再有单独的“graph” GPU 模型）。
   需要逐像素求值的图（texture/procedural/custom-node GLSL，其中
   `includes` 生效）会通过编译后的**shader-material 路径**渲染
   （`GpuMaterialModel::eShaderMaterial`，`MeshMaterialBackend` 的 `.material.frag`）。
2. **deferred pass 中的 lighting 固定为 Cook-Torrance。** 自定义 BXDF 的
   *lighting 响应*需要一条由材质 shader 自行求值
   光照的 forward 路径（Forward+）。注册表 + Lua 描述符的基础已经就绪：
   pipeline 资产可以声明 `ShadingModel{ name=…, bxdfLibrary=…, bxdfArtifact=…,
   bxdfFunction=…, extraParamSize=… }` 条目，`DeclarativeRenderer` 会把它们解析进
   其 `ShadingModelRegistry`（代码从 `kFirstCustomShadingModelCode`=8 开始分配）。
   `vultra.output.custom` 图节点通过 `shadingModelName` 选择其中之一。**尚未
   接通：** `ShadingModelParamsBuffer` 的 GPU 绑定以及生成的
   `vultra_eval_bxdf` 分派 —— deferred lighting shader 没有自定义分派，因此
   自定义模型材质目前以**默认 PBR 响应**渲染。自定义
   BXDF *shading* 将随 forward/clustered 路径（或一个运行时重新编译的 deferred
   变体）落地，这是剩下的部分。

因此，helper-function-include 机制与 `project.bxdf.sheen` 示例现在已经
真实存在且通过 codegen 验证；shading-model 注册表 + `ShadingModel{}` 描述符
已接通；完整的运行时自定义 BXDF **shading** 将随 forward 材质路径落地。
