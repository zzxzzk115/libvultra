# Get started

Choose the modules your program needs and add more as the experiment grows. The programs below are complete
alternatives for `main.cpp`; none uses example-only helpers. Fallible calls return
`std::expected<T, vultra::Error>` instead of throwing; each program returns the first error to `main`, which logs
its message and exits with status 1. Install the prerequisites in
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

class ClearApp final : public vultra::DesktopApp
{
public:
    using DesktopApp::DesktopApp;

private:
    std::expected<void, vultra::Error> onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const auto view = target.view();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        const float clear[4] {0.02f, 0.04f, 0.08f, 1};
        vultra::beginColorPass(getDevice(), cmd, *view, getSwapchain().size(), clear);
        getDevice().core.CmdEndRendering(cmd);
        return {};
    }
};

std::expected<void, vultra::Error> run()
{
    const vultra::AppConfig config {.title = "My study", .size = {640, 360}, .swapchainFormat = VriFormat_BGRA8_SRGB};
    auto                    services = vultra::DesktopApp::createServices(config);
    if (!services)
    {
        return std::unexpected(services.error());
    }
    ClearApp app(std::move(*services), config);
    return app.run();
}

int main()
{
    if (auto result = vultra::Logger::configure().and_then(run); !result)
    {
        vultra::Logger::app().error("{}", result.error().message);
        return 1;
    }
    return 0;
}
```

```sh
xmake f -m release --libvultra_features=app -y
xmake build study
xmake run study
```

Close the window or press Escape to exit; `app.run(60)` stops after 60 frames. Put logic in `onUpdate(float)`,
CPU render preparation in `onPreRender()` and GPU commands in `onRender()`. `DesktopApp` submits, waits and
presents; readback belongs in `onPostRender()`, after the frame completed. Every hook returns
`std::expected<void, vultra::Error>`; `run()` stops at the first error and returns it.

`DesktopApp::createServices()` creates the window, device and swapchain before the application exists. An
application that owns GPU objects of its own creates them from `services.device()` in a static `create()` and passes
them to a private constructor, as the [VRI triangle](../examples/basics/vri_triangle.cpp) does.

## Offline RenderGraph

This renders one frame without a window or swapchain and writes `output/graph.png`; the linear clear color is
stored as approximately RGBA `(39, 56, 80, 255)`. Passes declare their resource use and the graph records the
transitions:

```cpp
#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/image_readback.hpp>
#include <vultra/graph/render_graph.hpp>

std::expected<void, vultra::Error> run()
{
    auto device = vultra::Device::create();
    if (!device)
    {
        return std::unexpected(device.error());
    }
    vultra::RenderGraph      graph(**device);
    constexpr vultra::Extent size {640, 360};
    const auto               color = graph.createTexture("color", vultra::colorTexture(size, VriFormat_RGBA8_SRGB));
    if (!color)
    {
        return std::unexpected(color.error());
    }
    if (auto added = graph.addPass("Clear",
                                   {{*color, vultra::Usage::eColorWrite}},
                                   [&device = **device, color = *color, size](
                                       VriCommandBuffer*    cmd,
                                       vultra::RenderGraph& active) -> std::expected<void, vultra::Error>
                                   {
                                       const auto view = active.getTexture(color).view();
                                       if (!view)
                                       {
                                           return std::unexpected(view.error());
                                       }
                                       const float clear[4] {0.02f, 0.04f, 0.08f, 1};
                                       vultra::beginColorPass(device, cmd, *view, size, clear);
                                       device.core.CmdEndRendering(cmd);
                                       return {};
                                   });
        !added)
    {
        return added;
    }
    if (auto exported = graph.exportResource(*color); !exported)
    {
        return exported;
    }
    if (auto compiled = graph.compile(); !compiled)
    {
        return compiled;
    }

    auto frame = vultra::Frame::create(**device);
    if (!frame)
    {
        return std::unexpected(frame.error());
    }
    const auto cmd = (*frame)->begin();
    if (!cmd)
    {
        return std::unexpected(cmd.error());
    }
    if (auto executed = graph.execute(*cmd); !executed)
    {
        return executed;
    }
    if (auto completed = (*frame)->submitAndWait(); !completed)
    {
        return completed;
    }
    const auto image = vultra::readback(**device, graph.getTexture(*color));
    if (!image)
    {
        return std::unexpected(image.error());
    }
    return vultra::savePng(*image, "output/graph.png");
}

int main()
{
    if (auto result = vultra::Logger::configure().and_then(run); !result)
    {
        vultra::Logger::app().error("{}", result.error().message);
        return 1;
    }
    return 0;
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
executed with `graph.execute(frame)` after `frame.begin()` instead, which submits every batch but the final graphics one.

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

std::expected<void, vultra::Error> run()
{
    auto device = vultra::Device::create();
    if (!device)
    {
        return std::unexpected(device.error());
    }
    const auto asset = vultra::importAsset("resources/models/DamagedHelmet/DamagedHelmet.glb",
                                           {.cache = false}); // Do not write an import cache.
    if (!asset)
    {
        return std::unexpected(asset.error());
    }
    auto scene = vultra::GpuScene::create(**device, *asset);
    if (!scene)
    {
        return std::unexpected(scene.error());
    }
    auto environment = vultra::Environment::create(**device); // Built-in analytic studio environment.
    if (!environment)
    {
        return std::unexpected(environment.error());
    }
    auto path = vultra::ForwardPath::create(**device);
    if (!path)
    {
        return std::unexpected(path.error());
    }
    vultra::RenderGraph graph(**device);

    constexpr vultra::Extent size {640, 360};
    const auto               resources = (*path)->addPasses(graph, **scene, **environment, size);
    if (!resources)
    {
        return std::unexpected(resources.error());
    }
    const auto hdr = vultra::findSceneResource(*resources, vultra::kSceneHdr);
    if (!hdr)
    {
        return std::unexpected(hdr.error());
    }
    if (auto exported = graph.exportResource(*hdr); !exported)
    {
        return exported;
    }
    if (auto compiled = graph.compile(); !compiled)
    {
        return compiled;
    }

    const glm::vec3            center   = (*scene)->center;
    const float                radius   = (*scene)->radius;
    const float                distance = radius * 3;
    const vultra::RenderCamera camera {
        glm::lookAtRH(center + glm::vec3(0, 0, distance), center, glm::vec3(0, 1, 0)),
        glm::perspectiveRH_ZO(glm::radians(45.0f), float(size.width) / float(size.height), radius, distance * 2),
        radius,
        distance * 2};
    const vultra::RenderLight sun {.kind             = vultra::RenderLightKind::eDirectional,
                                   .directionToLight = {-0.5f, 0.8f, 0.4f},
                                   .intensity        = 3};
    if (auto prepared = (*path)->prepare(graph, camera, std::span(&sun, 1)); !prepared)
    {
        return prepared;
    }

    auto frame = vultra::Frame::create(**device);
    if (!frame)
    {
        return std::unexpected(frame.error());
    }
    const auto cmd = (*frame)->begin();
    if (!cmd)
    {
        return std::unexpected(cmd.error());
    }
    if (auto executed = graph.execute(*cmd); !executed)
    {
        return executed;
    }
    if (auto completed = (*frame)->submitAndWait(); !completed)
    {
        return completed;
    }
    const auto image = vultra::readback(**device, graph.getTexture(*hdr));
    if (!image)
    {
        return std::unexpected(image.error());
    }
    return vultra::savePfm(*image, "build/.tmp/forward.pfm");
}

int main()
{
    if (auto result = vultra::Logger::configure().and_then(run); !result)
    {
        vultra::Logger::app().error("{}", result.error().message);
        return 1;
    }
    return 0;
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
