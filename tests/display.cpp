#include "../examples/common/triangle.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/renderer/texture_blit.hpp>
#include <vultra/function/research/capture.hpp>

#include <GLFW/glfw3.h>

#include <cmath>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void testSwapchainColor()
    {
        using namespace vultra;
        Window window("Swapchain color test", {160, 120});
        Device device;
        Frame  frame(device);
        for (const auto format : {VriFormat_BGRA8_UNORM, VriFormat_BGRA8_SRGB})
        {
            Swapchain swapchain(device, window, format);
            for (int pass = 0; pass < 2; ++pass)
            {
                glfwSetWindowSize(window.handle(), 160 + pass * 32, 120 + pass * 24);
                Texture* target = nullptr;
                for (int attempt = 0; attempt < 10 && !target; ++attempt)
                {
                    window.poll();
                    target = swapchain.acquire();
                }
                require(target != nullptr && target->desc.format == format,
                        "Swapchain did not retain its color format");
                auto* cmd = frame.begin();
                target->transition(cmd,
                                   {VriAccess_ColorAttachmentWrite,
                                    VriLayout_ColorAttachment,
                                    VriPipelineStage_ColorAttachmentOutput});
                const float clear[] {0.2f, 0.3f, 0.3f, 1};
                beginColorPass(device, cmd, target->view(), swapchain.size(), clear);
                device.core.CmdEndRendering(cmd);
                frame.submitAndWait();
                const auto  pixels   = readback(device, *target);
                const float expected = format == VriFormat_BGRA8_SRGB ? 124.0f / 255 : 0.2f;
                require(std::abs(pixels.rgba[0] - expected) < 0.005f,
                        "Swapchain clear color has an incorrect transfer");
                cmd = frame.begin();
                target->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
                frame.submitAndWait();
                swapchain.present();
            }
        }
    }

    class ViewportApp final : public vultra::ImGuiApp
    {
    public:
        ViewportApp() :
            ImGuiApp({"Vultra | Docking and Viewport Test", {400, 300}}, {.persistLayout = false})
        {
            ImGui::GetIO().ConfigViewportsNoAutoMerge = true;
        }

        bool created      = false;
        bool resized      = false;
        bool docked       = false;
        bool released     = false;
        bool minimizedGui = false;
        bool closed       = false;

    private:
        void onUpdate(float) override
        {
            if (frameCount() == 2 && !m_Minimized)
            {
                glfwIconifyWindow(getWindow().handle());
                m_Minimized = true;
            }
            if (glfwGetWindowAttrib(getWindow().handle(), GLFW_ICONIFIED) && ++m_MinimizedTicks == 3)
            {
                glfwRestoreWindow(getWindow().handle());
            }
            const auto& viewports = ImGui::GetPlatformIO().Viewports;
            for (int i = 1; i < viewports.Size; ++i)
            {
                auto* viewport = viewports[i];
                if (viewport->RendererUserData && viewport->PlatformHandle)
                {
                    created    = true;
                    int width  = 0;
                    int height = 0;
                    glfwGetWindowSize(static_cast<GLFWwindow*>(viewport->PlatformHandle), &width, &height);
                    resized = resized || (width == 320 && height == 220);
                    if (frameCount() == 12)
                    {
                        viewport->PlatformRequestClose = true;
                    }
                }
            }
            if (frameCount() >= 7 && frameCount() <= 9)
            {
                released = released || viewports.Size == 1;
            }
        }

        void onImGui() override
        {
            if (!m_Open)
            {
                closed = true;
                return;
            }
            const bool minimized  = glfwGetWindowAttrib(getWindow().handle(), GLFW_ICONIFIED) != 0;
            minimizedGui          = minimizedGui || minimized;
            const auto origin     = ImGui::GetMainViewport()->Pos;
            const bool shouldDock = frameCount() >= 5 && frameCount() <= 9;
            ImGui::SetNextWindowDockID(shouldDock ? getGui().dockspaceId() : 0, ImGuiCond_Always);
            if (!shouldDock && !minimized)
            {
                ImGui::SetNextWindowPos({origin.x + 430, origin.y + 20}, ImGuiCond_Always);
                ImGui::SetNextWindowSize(frameCount() < 2 ? ImVec2(240, 180) : ImVec2(320, 220), ImGuiCond_Always);
            }
            ImGui::Begin("Detachable panel", &m_Open);
            docked = docked || ImGui::IsWindowDocked();
            ImGui::TextUnformatted("Independent swapchain, then dock and close");
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                {ImGui::GetCursorScreenPos().x + 60, ImGui::GetCursorScreenPos().y + 40},
                IM_COL32(255, 80, 0, 255));
            ImGui::End();
        }

        void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
        {
            target.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[4] {0.1f, 0.2f, 0.3f, 1};
            vultra::beginColorPass(getDevice(), cmd, target.view(), getSwapchain().size(), clear);
            getDevice().core.CmdEndRendering(cmd);
            drawGui(cmd, target);
        }

        bool     m_Open           = true;
        bool     m_Minimized      = false;
        uint32_t m_MinimizedTicks = 0;
    };

    void testGuiScale()
    {
        using namespace vultra;
        Window  window("Vultra | ImGui Pixel Scale Test", {320, 240});
        Device  device;
        Frame   frame(device);
        Texture target(device, colorTexture({640, 480}, VriFormat_BGRA8_UNORM));
        Gui gui(device, window, target.desc.format, {.docking = false, .multiViewport = false, .persistLayout = false});
        gui.begin();
        auto* list = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
        list->PushClipRect({10, 10}, {20, 20});
        list->AddRectFilled({0, 0}, {60, 60}, IM_COL32(255, 0, 0, 255));
        list->PopClipRect();
        gui.upload({640, 480});
        auto* cmd = frame.begin();
        gui.copy(cmd);
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float black[4] {0, 0, 0, 1};
        beginColorPass(device, cmd, target.view(), {640, 480}, black);
        gui.draw(cmd);
        device.core.CmdEndRendering(cmd);
        frame.submitAndWait();
        const auto image = readback(device, target);
        require(image.rgba[(30 * 640 + 30) * 4] > 0.99f, "GUI clipping did not scale with the framebuffer");
        require(image.rgba[(15 * 640 + 15) * 4] == 0 && image.rgba[(45 * 640 + 45) * 4] == 0,
                "GUI clipping leaked outside the scaled rectangle");
    }

    void testMirror()
    {
        using namespace vultra;
        Device      device;
        Frame       frame(device);
        Texture     output(device, colorTexture({128, 64}));
        TextureBlit blit(device, output.desc.format, 2);
        for (auto format : {VriFormat_RGBA8_SRGB, VriFormat_RGBA8_UNORM, VriFormat_BGRA8_SRGB, VriFormat_BGRA8_UNORM})
        {
            Texture left(device, colorTexture({32, 32}, format));
            Texture right(device, colorTexture({32, 32}, format));
            blit.setSource(0, left);
            blit.setSource(1, right);
            auto*       cmd = frame.begin();
            const float leftColor[4] {0.18f, 0.04f, 0.5f, 1};
            const float rightColor[4] {0.5f, 0.18f, 0.04f, 1};
            for (auto* source : {&left, &right})
            {
                source->transition(cmd,
                                   {VriAccess_ColorAttachmentWrite,
                                    VriLayout_ColorAttachment,
                                    VriPipelineStage_ColorAttachmentOutput});
                beginColorPass(device, cmd, source->view(), {32, 32}, source == &left ? leftColor : rightColor);
                device.core.CmdEndRendering(cmd);
            }
            output.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float black[4] {0, 0, 0, 1};
            beginColorPass(device, cmd, output.view(), {128, 64}, black);
            device.core.CmdEndRendering(cmd);
            blit.draw(cmd, output, {8, 8, 48, 48}, 0, true);
            blit.draw(cmd, output, {72, 8, 48, 48}, 1, true);
            frame.submitAndWait();
            const auto image = readback(device, output);
            for (uint32_t eye = 0; eye < 2; ++eye)
            {
                const auto  pixel    = (32 * 128 + 32 + eye * 64) * 4;
                const auto* expected = eye == 0 ? leftColor : rightColor;
                for (size_t channel = 0; channel < 3; ++channel)
                {
                    const auto encoded = 1.055f * std::pow(expected[channel], 1.0f / 2.4f) - 0.055f;
                    require(std::abs(image.rgba[pixel + channel] - encoded) < 0.01f,
                            "XR mirror sRGB/UNORM conversion or eye binding mismatch");
                }
            }
            require(image.rgba[0] == 0 && image.rgba[(32 * 128 + 64) * 4] == 0, "Blit ignored its output rectangle");
        }
        // Shared authored triangle colors must agree between desktop UNORM and an sRGB XR mirror.
        Texture  desktop(device, colorTexture({128, 64}));
        Texture  eye(device, colorTexture({128, 64}, VriFormat_RGBA8_SRGB));
        Triangle normal(device, desktop.desc.format, "examples/research/shaders/triangle.slang");
        Triangle xr(device, eye.desc.format, "examples/research/shaders/triangle.slang");
        xr.parameters.decodeSrgb = 1;
        auto*       cmd          = frame.begin();
        const float black[4] {0, 0, 0, 1};
        for (auto* texture : {&desktop, &eye})
        {
            texture->transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        }
        normal.draw(cmd, desktop, black);
        xr.draw(cmd, eye, black);
        blit.setSource(0, eye);
        blit.draw(cmd, output, {0, 0, 128, 64}, 0, true);
        frame.submitAndWait();
        const auto reference = readback(device, desktop);
        const auto mirrored  = readback(device, output);
        for (size_t i = 0; i < reference.rgba.size(); ++i)
        {
            require(std::abs(reference.rgba[i] - mirrored.rgba[i]) < 0.01f,
                    "XR triangle differs from desktop display colors");
        }
    }
} // namespace

int main()
try
{
    {
        ViewportApp app;
        app.run(16);
        require(app.created && app.resized, "Platform viewport was not created/resized");
        require(app.docked && app.released, "Docking did not release the platform viewport");
        require(app.minimizedGui, "Detached GUI stopped while main window was minimized");
        require(app.closed, "Platform close request did not close the panel");
    }
    testGuiScale();
    testMirror();
    testSwapchainColor();
    vultra::Logger::app().info(
        "Display tests passed: viewport create/resize/dock/undock/close, four XR formats, stereo "
        "mirror and triangle color parity");
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
