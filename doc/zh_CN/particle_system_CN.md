# Particle system

[English](../particle_system.md) | **简体中文**

一个由 `ParticleEmitterComponent` 驱动的粒子系统，带有两个可互换的后端。

> **状态：** 默认后端在 **GPU 计算着色器**中模拟粒子，并通过内置渲染图通道将它们渲染为
> 实例化的、面向相机的**加色台球面片（billboard）**（柔和、带深度淡化）。一个 **CPU**
> 后端（调试绘制预览）作为回退保留。组件上的 `gpu` 字段选择后端（默认 `true` = GPU）。

## Component

[`ParticleEmitterComponent`](../../source/vultra/include/vultra/function/world/components/particle_emitter_component.hpp)
附加到一个同时拥有 `TransformComponent` 的实体上；粒子从该实体的世界位置生成。

| 字段 | 含义 |
|-------|---------|
| `playing` | 发射或暂停 |
| `worldSpace` | 仅作创作用途的空操作（见路线图）：模拟原点始终是发射器的世界矩阵；局部空间模拟尚未实现 |
| `gpu` | 后端选择：GPU 计算 + billboard（`true`，默认）或 CPU 调试绘制（`false`） |
| `maxParticles` | 存活粒子的硬性上限（GPU：固定池大小） |
| `emissionRate` | 每秒生成的粒子数 |
| `lifetime` / `lifetimeVariance` | 秒，± 比例 |
| `spawnRadius` | 围绕发射器的随机生成偏移 |
| `startVelocity` / `velocityVariance` | 初始速度 + 随机幅度 |
| `gravity` | 恒定加速度 |
| `startSize` / `endSize` | 在生命周期内插值的大小 |
| `startColor` / `endColor` | 在生命周期内插值的颜色（RGBA） |

该组件已注册用于反射、场景（反）序列化、场景组件注册表、编辑器检查器以及添加组件菜单，
因此它能在 `.vscn`/`.vmanifest` 中往返，并可在编辑器中完全编辑。

## GPU backend (default)

当 `gpu == true` 时，发射器完全在 GPU 上驱动：

1. **收集 + 管理** —— `RenderWorldCooker::cook` 将 GPU 发射器收集进 `RenderWorld::emitters`。
   [`GpuParticleManager`](../../source/vultra/include/vultra/function/particle/gpu_particle_manager.hpp)
   （由 `RenderSystem` 拥有）为每个发射器保持一个持久的粒子 SSBO（一个由
   `maxParticles` 个 [`GpuParticle`](../../source/vultra/include/vultra/function/particle/gpu_particle.hpp)
   组成的固定池），每帧推进一个 CPU 发射累加器 + 轮转生成游标，并将每个发射器的绘制列表
   发布到激活的 `GpuSceneView` 上。
2. **模拟** —— `ParticleSimulatePass`（计算）重新生成轮转生成窗口并对其余粒子进行积分
   （`velocity += gravity·dt; position += velocity·dt`），使粒子老化消亡。死亡的槽位
   坍缩为一个退化的 billboard。
3. **渲染** —— `ParticleRenderPass`（图形）为每个池槽位绘制一个实例化的、面向相机的
   billboard，使用加色混合以及一个柔和的圆形精灵 + 相对于场景深度的柔和深度淡化，
   在色调映射之前以 HDR 进行。

这并不是两个独立的图节点：一个单独的内置 `ParticleRender` 图节点 —— 在
`universal.vrg.json`（以及项目的 `default.vrg.json`）中接入，位于
`GeneralGaussianSplatComposite` 和 `Ssr` 之间 —— 在内部运行这两个通道。它的处理器
依次调用 `ParticleSimulatePass`（计算）然后 `ParticleRenderPass`（图形）。内置图还
附带层级变体 `universal_compat.vrg.json` 和 `universal_rt.vrg.json`（默认文件仍命名为
`universal.vrg.json`）。

## CPU backend (fallback)

当 `gpu == false` 时，
[`ParticleSystem`](../../source/vultra/include/vultra/function/particle/particle_system.hpp) ——
一个**在** `RenderSystem` 之前放置的引擎子系统 —— 在 CPU 上进行模拟，并通过调试绘制路径
预览每个存活的粒子。它会跳过 `gpu == true` 的发射器。它在
[demo_app_host.cpp](../../source/vultra/src/core/app/demo_app_host.cpp) 中接入标准子系统集。

## Editor

- **Add Component → Rendering → Particle Emitter** 添加该组件；所有字段都可在检查器中
  编辑（反射驱动）。
- 场景视图在每个发射器处显示一个 ✦（`ICON_MDI_CREATION`）gizmo 图标，可点击选择。
- 示例场景 [resources/scenes/test.vmanifest](../../resources/scenes/test.vmanifest) 有一个
  "Sparks" 发射器供参考。

## Implementation notes

- GPU 池是一个**固定大小、轮转**的环（`maxParticles` 个槽位）。CPU 推进一个发射累加器和
  一个生成游标；计算着色器重新生成 `[cursor, cursor+emitCount)` 窗口并对其余部分积分。
  一个新创建/调整大小的池会设置一个重置位，以便着色器在没有主机清除的情况下为其播种。
  目前还没有 GPU 的死亡/存活列表、间接 dispatch 或前后排序 —— 重叠的半透明粒子使用加色
  混合（顺序无关）。
- 池按发射器实体键控并跨帧持久化；即使一个发射器由多个视图（例如编辑器场景视图 + 游戏
  视图）渲染，发射每帧也最多推进一次。

## Roadmap

- 纹理/图集精灵、发射形状、颜色/大小随生命周期变化的曲线以及子发射器。
- 存活/死亡列表回收 + 间接 dispatch/draw 以及可选的深度排序（RHI 已经有一个基数排序器），
  用于大型 alpha 混合系统。
- 局部空间模拟：`worldSpace` 字段目前会被反射/序列化/可脚本化，但两个后端都没有读取它
  （发射器的世界矩阵始终是原点），因此在局部空间集成落地之前，它是一个仅作创作用途的空操作。
