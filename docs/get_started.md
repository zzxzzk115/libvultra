# Get started

Choose the modules your program needs and add more as the experiment grows. The programs below are complete
alternatives for `main.cpp`; none uses example-only helpers. Install the prerequisites in
[BUILD.md](../BUILD.md#prerequisites) first. The library is verified on Windows with Vulkan only.

| Start here | Module | What your code owns |
| --- | --- | --- |
| [Window](#window) | `app` | Nothing: `DesktopApp` owns the window, device, frame and presentation. |
| [Offline RenderGraph](#offline-rendergraph) | `graph` | The device, the graph and the completed GPU submission. |
| [Scene with ForwardPath](#scene-with-forwardpath) | `render` | Also the imported scene, environment and render path. |

## Create a project

Place a checkout of libvultra under `external/libvultra` in your own project, next to `main.cpp` and this
`xmake.lua`:

```lua
set_project("study")
set_languages("cxx23")
add_rules("mode.debug", "mode.release")
if is_plat("windows") then
    set_runtimes(is_mode("debug") and "MTd" or "MT")
end
includes("external/libvultra")

target("study")
    set_kind("binary")
    add_deps("vultra")
    add_files("main.cpp")
    set_rundir("$(projectdir)")
target_end()
```

Run each section's commands from the project directory; on Windows use PowerShell or cmd. An embedded libvultra
does not build its own examples, tests or tools, and module dependencies are selected automatically.

## Window

This creates a window and clears it each frame:

```cpp
#include <vultra/app/desktop_app.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/resources.hpp>

#include <exception>

class ClearApp final : public vultra::DesktopApp
{
public:
    ClearApp() :
        vultra::DesktopApp({.title = "My study", .size = {640, 360}, .swapchainFormat = VriFormat_BGRA8_SRGB})
    {
    }

private:
    void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0.02f, 0.04f, 0.08f, 1};
        vultra::beginColorPass(getDevice(), cmd, target.view(), getSwapchain().size(), clear);
        getDevice().core.CmdEndRendering(cmd);
    }
};

int main()
try
{
    vultra::Logger::configure();
    ClearApp app;
    app.run();
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
```

```sh
xmake f -m release --libvultra_features=app -y
xmake build study
xmake run study
```

Close the window or press Escape to exit; `app.run(60)` stops after 60 frames. Put logic in `onUpdate(float)`,
CPU render preparation in `onPreRender()` and GPU commands in `onRender()`. `DesktopApp` submits, waits and
presents; readback belongs in `onPostRender()`, after the frame completed.

## Offline RenderGraph

This renders one frame without a window or swapchain and writes `output/graph.png`; the linear clear color is
stored as approximately RGBA `(39, 56, 80, 255)`. Passes declare their resource use and the graph records the
transitions:

```cpp
#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/image_readback.hpp>
#include <vultra/graph/render_graph.hpp>

#include <exception>

int main()
try
{
    vultra::Logger::configure();
    vultra::Device           device;
    vultra::RenderGraph      graph(device);
    constexpr vultra::Extent size {640, 360};
    const auto               color = graph.createTexture("color", vultra::colorTexture(size, VriFormat_RGBA8_SRGB));

    graph.addPass("Clear",
                  {{color, vultra::Usage::eColorWrite}},
                  [&device, color, size](VriCommandBuffer* cmd, vultra::RenderGraph& active)
                  {
                      const float clear[4] {0.02f, 0.04f, 0.08f, 1};
                      vultra::beginColorPass(device, cmd, active.getTexture(color).view(), size, clear);
                      device.core.CmdEndRendering(cmd);
                  });
    graph.exportResource(color);
    graph.compile();

    vultra::Frame frame(device);
    graph.execute(frame.begin());
    frame.submitAndWait();
    vultra::savePng(vultra::readback(device, graph.getTexture(color)), "output/graph.png");
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
```

```sh
xmake f -m release --libvultra_features=graph -y
xmake build study
xmake run study
```

`eColorWrite` declares the transition the window program recorded by hand. Exporting the result keeps its
producing pass alive; unobserved passes are culled. `Frame::submitAndWait()` is the completion boundary;
`readback()` then performs its own blocking transfer. For repeated frames, reuse the graph and execute it again.
`graph.writeDot(stream)` writes the compiled plan for Graphviz. A graph with `PassQueue::eAsyncCompute` passes is
executed with `frame.begin(); graph.execute(frame);` instead, which submits every batch but the final graphics one.

## Scene with ForwardPath

A render path turns a scene into named graph resources. This imports a model, renders it with the reference
`ForwardPath` and writes the scene-linear `scene.hdr` result:

```cpp
#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/image_readback.hpp>
#include <vultra/render/environment.hpp>
#include <vultra/render/forward_path.hpp>
#include <vultra/render/scene.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <exception>

int main()
try
{
    vultra::Logger::configure();
    vultra::Device      device;
    const auto          asset = vultra::importAsset("resources/models/DamagedHelmet/DamagedHelmet.glb",
                                                    {.cache = false}); // Do not write an import cache.
    vultra::GpuScene    scene(device, asset);
    vultra::Environment environment(device); // Built-in analytic studio environment.
    vultra::ForwardPath path(device);
    vultra::RenderGraph graph(device);

    constexpr vultra::Extent size {640, 360};
    const auto               resources = path.addPasses(graph, scene, environment, size);
    const auto               hdr       = vultra::findSceneResource(resources, vultra::kSceneHdr);
    graph.exportResource(hdr);
    graph.compile();

    const float                distance = scene.radius * 3;
    const vultra::RenderCamera camera {
        glm::lookAtRH(scene.center + glm::vec3(0, 0, distance), scene.center, glm::vec3(0, 1, 0)),
        glm::perspectiveRH_ZO(glm::radians(45.0f), float(size.width) / float(size.height), scene.radius, distance * 2),
        scene.radius,
        distance * 2};
    const vultra::RenderLight sun {.kind             = vultra::RenderLightKind::eDirectional,
                                   .directionToLight = {-0.5f, 0.8f, 0.4f},
                                   .intensity        = 3};
    path.prepare(graph, camera, std::span(&sun, 1));

    vultra::Frame frame(device);
    graph.execute(frame.begin());
    frame.submitAndWait();
    vultra::savePfm(vultra::readback(device, graph.getTexture(hdr)), "build/.tmp/forward.pfm");
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
```

`ForwardPath` and `Environment` compile the library shaders from `builtin/shaders` relative to the working
directory, and the model path is relative to the libvultra checkout. For this program set
`set_rundir("$(projectdir)/external/libvultra")` in `xmake.lua`; the output path is then relative to it as well.

```sh
xmake f -m release --libvultra_features=render -y
xmake build study
xmake run study
```

Call `prepare()` once per frame after `compile()` and before recording. The path borrows the scene and
environment until the last GPU use of the graph completes, so declare them before the path and the graph. Later
passes depend only on resource names such as `scene.hdr` and `scene.depth`.

## Next steps

- Debug UI: select `app,ui`, derive from [ImGuiApp](../source/app/include/vultra/app/imgui_app.hpp), build UI in
  `onImGui()` and call `drawGui()` in `onRender()` after scene rendering; see the [UI example](../examples/ui/main.cpp).
- Geometry and Slang shaders (add the `shader` module): the [VRI triangle](../examples/basics/vri_triangle.cpp);
  compute and `.vgraph` definitions: the [offline example](../examples/offline/offline.cpp).
- A custom render path: [render_path.hpp](../source/render/include/vultra/render/render_path.hpp) and `NormalPath`
  in [tests/render_path.cpp](../tests/render_path.cpp).
- Reference: [BUILD.md](../BUILD.md), [architecture.md](architecture.md) and [roadmap.md](roadmap.md).
