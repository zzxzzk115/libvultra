#include "window_events.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/swapchain.hpp>
#include <vultra/platform/window.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <chrono>
#include <memory>
#include <string_view>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
{
    using namespace vultra;
    auto   first = std::make_unique<Window>("Vultra | First Owned Window", Extent {320, 240});
    Window window("Vultra | Window Backend Test", {320, 240});
    first->poll();
    window.poll();
    {
        test::WindowEvents events(*first);
        events.focus(true);
        events.pressForward();
        window.poll();
        first->poll();
        require(first->input().isKeyHeld(KeyCode::eW), "Native input did not reach its owning window");
        require(!window.input().isKeyHeld(KeyCode::eW), "Input leaked to another owned window");
    }
    first.reset();
    require(window.poll() && !window.framebufferSize().empty(), "Destroying a sibling terminated the window runtime");
    window.setTitle("Vultra | Surviving Window");
    require(window.title() == "Vultra | Surviving Window", "Surviving window cannot update its title");
    {
        Window third("Vultra | Recreated Window", {160, 120});
        require(third.poll(), "Cannot create another window after sibling destruction");
    }
    const auto native = platform::nativeWindow(window);
#if defined(__linux__)
    if (const auto* requested = platform::requestedWindowSystem())
    {
        const bool wayland = std::string_view(requested) == "wayland";
        require(native.type == (wayland ? VriWindowSystem_Wayland : VriWindowSystem_Xlib),
                "The requested display system was not selected");
    }
#endif
    require(window.nativeHandle() != nullptr, "Missing native window handle");
    Device      device;
    Swapchain   swapchain(device, window, VriFormat_BGRA8_UNORM);
    Frame       frame(device);
    EditorGui   gui(device, window, swapchain.format(), {.persistLayout = false});
    const auto& io = ImGui::GetIO();
    require((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0, "Docking is unavailable");
    if (native.type == VriWindowSystem_Wayland)
    {
        require(!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable),
                "Wayland advertises unsupported detached viewports");
    }
    else
    {
        require((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0, "Desktop detached viewports were lost");
    }
    const auto capture =
        std::filesystem::path("build/.tmp/window-backends") /
        ("window-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    for (int index = 0; index < 3; ++index)
    {
        require(window.poll(), "Window closed before rendering completed");
        auto* target = swapchain.acquire();
        require(target != nullptr, "Cannot acquire the native swapchain");
        gui.begin();
        const auto origin = ImGui::GetMainViewport()->Pos;
        ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
            ->AddRectFilled({origin.x + 8, origin.y + 8}, {origin.x + 40, origin.y + 40}, IM_COL32(255, 0, 0, 255));
        gui.upload(swapchain.size());
        auto* cmd = frame.begin();
        gui.copy(cmd);
        target->transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 1, 1};
        beginColorPass(device, cmd, target->view(), swapchain.size(), clear);
        gui.draw(cmd);
        device.core.CmdEndRendering(cmd);
        target->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
        frame.submitAndWait();
        const auto image   = readback(device, *target);
        const auto logical = window.size();
        const auto x       = 16 * image.size.width / logical.width;
        const auto y       = 16 * image.size.height / logical.height;
        const auto pixel   = (y * image.size.width + x) * 4;
        if (!(image.rgba[pixel] > 0.99f && image.rgba[pixel + 2] < 0.01f))
        {
            savePng(image, capture);
            Logger::app().error("GUI pixel {}, {} is {}, {}, {}; logical {}x{}, pixels {}x{}, origin {}, {}",
                                x,
                                y,
                                image.rgba[pixel],
                                image.rgba[pixel + 1],
                                image.rgba[pixel + 2],
                                logical.width,
                                logical.height,
                                image.size.width,
                                image.size.height,
                                origin.x,
                                origin.y);
        }
        require(image.rgba[pixel] > 0.99f && image.rgba[pixel + 2] < 0.01f,
                "GUI does not render at the native framebuffer scale");
        require(image.rgba[2] > 0.99f, "Native swapchain clear/readback is incorrect");
        if (index == 2)
        {
            savePng(image, capture);
        }
        swapchain.present();
        gui.renderPlatformWindows();
    }
    Logger::app().info(
        "Window backend tests passed: independent ownership/input, native swapchain, GUI scale and docking; system {}",
        int(native.type));
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
