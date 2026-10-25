# Get started

Choose the services your program needs, then add capabilities as the experiment grows.
The programs below are complete alternatives for `main.cpp`; none uses example-only helpers.
Install the prerequisites in [BUILD.md](../BUILD.md#prerequisites) first.

| Start here | Selected features | What it owns |
| --- | --- | --- |
| [Desktop graphics](#desktop-graphics) | `desktop` | DesktopApp owns the window, device, frame and presentation. |
| [Offline graphics](#offline-graphics) | `gpu,image_io` | Your code owns the device, texture and completed GPU submission. |
| [Offline RenderGraph](#offline-rendergraph) | `graph,image_io` | Your code also owns the graph and declares each pass's resource use. |

All three clear a 640×360 image to the same dark blue. With the shown SRGB formats, the expected PNG pixel
is approximately RGBA `(39, 56, 80, 255)`; the original linear clear is `(0.02, 0.04, 0.08, 1)`.

## Create a project

Place a checkout of libvultra under `external/libvultra` in your own project:

```text
study/
  external/libvultra/
  main.cpp
  xmake.lua
```

Use this `xmake.lua` for any of the programs below:

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

Run each section's configure/build/run commands from `study/`. `-P .` selects this project explicitly, including
when it is nested under another xmake checkout. Embedded library builds leave repository
examples/tests disabled. These selections need neither the .NET SDK nor shader compilation.

## Desktop graphics

This creates a window and clears it each frame. Save as `main.cpp`:

```cpp
#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/main/app/desktop_app.hpp>

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
xmake f -P . -m release --libvultra_features=desktop -y
xmake build -P . study
xmake run -P . study
```

Close the window or press Escape to exit; `app.run(60)` runs at most 60 rendered frames.
Put logic in `onUpdate(float)`, CPU render preparation in `onPreRender()`, and GPU commands in `onRender()`.
DesktopApp submits, waits and presents. Readback belongs in `onPostRender()`, after completion.

For a platform-hosted version, derive from `<vultra/main/app/platform_app.hpp>`'s PlatformApp and pass an AppLaunch
to its constructor. Select `android_app` for GameActivity or `web_app` for requestAnimationFrame; the rendering
callbacks remain the same. Android's entry supplies its borrowed `android_app*`; a Web page owns the canvas size.
[Build and entry examples](../BUILD.md#android-and-web-applications) show packaging and the existing portable
clear/VRI/RenderGraph modes. These hosts do not make desktop GUI, scene or script dependencies portable automatically.

The SRGB attachment converts linear clear/draw colors to display values. To draw geometry next, follow the
[VRI triangle](../examples/basics/vri_triangle.cpp) and its explicit
[vertex/index/pipeline setup](../examples/common/colored_mesh.cpp). Add the `shader` feature when compiling Slang.

## Offline graphics

This records one GPU frame, waits for completion and writes `output/offline.png`.
It creates no window or swapchain. Replace `main.cpp` with:

```cpp
#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/device.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>

#include <exception>

int main()
try
{
    vultra::Logger::configure();
    vultra::Device           device;
    constexpr vultra::Extent size {640, 360};
    vultra::Texture          color(device, vultra::colorTexture(size, VriFormat_RGBA8_SRGB));
    vultra::Frame            frame(device);

    auto* cmd = frame.begin();
    color.transition(
        cmd,
        {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
    const float clear[4] {0.02f, 0.04f, 0.08f, 1};
    vultra::beginColorPass(device, cmd, color.view(), size, clear);
    device.core.CmdEndRendering(cmd);
    frame.submitAndWait();

    vultra::savePng(vultra::readback(device, color), "output/offline.png");
    vultra::Logger::app().info("Saved output/offline.png");
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
```

```sh
xmake f -P . -m release --libvultra_features=gpu,image_io -y
xmake build -P . study
xmake run -P . study
```

Offline still needs a GPU/driver. `Frame::submitAndWait()` is the completion boundary; `readback()` then performs
its own blocking transfer and returns owned CPU pixels. The SRGB texture already contains display-encoded values;
`savePng()` quantizes them without another gamma conversion. Use a float attachment and `savePfm()` for linear HDR.
The output path is relative to the project directory; rerunning replaces the image.

## Offline RenderGraph

Use a graph when passes need declared dependencies and automatic resource transitions.
This complete `main.cpp` produces the same clear image through one exported graph resource:

```cpp
#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

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
xmake f -P . -m release --libvultra_features=graph,image_io -y
xmake build -P . study
xmake run -P . study
```

`eColorWrite` declares the transition that the direct example records manually.
Exporting the result keeps its producing pass live. Device outlives the graph; captured owners and GPU resources
survive the completed submission. For repeated frames, reuse the graph/resources and execute again.

The [offline triangle/compute example](../examples/offline/main.cpp) adds native shaders, a project pass,
profiling, C++/JSON graph parity and separate linear/display outputs.

## Add higher capabilities when needed

| Need | Continue with |
| --- | --- |
| Debug UI | [ImGuiApp](../source/main/include/vultra/main/app/imgui_app.hpp) and [UI example](../examples/ui/main.cpp). Add `editor_gui` to `desktop`, construct UI in `onImGui()`, and record `drawGui()` after scene rendering. |
| Imported models and builtin rendering | Select `rendering,asset_import,scene_prepare,shader` for source assets/shaders. Cooked applications select `rendering` and supply SceneData, PreparedTextures and a shader AssetSource directly. |
| Explicit compute | Select `compute` and use ComputeContext for typed scalar buffers, Slang kernels and readback. |

Use [BUILD.md](../BUILD.md) for platform/setup/export details, [architecture.md](architecture.md) for ownership
and subsystem boundaries, and [roadmap.md](roadmap.md) for capabilities that are not yet implemented.
