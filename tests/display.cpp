#include "../examples/common/colored_mesh.hpp"
#include "../examples/common/triangle.hpp"
#include "support/check.hpp"

#include <vultra/app/imgui_app.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/image_readback.hpp>
#include <vultra/render/texture_blit.hpp>

#include <cmath>

namespace
{
    using support::must;
    using support::require;

    using Result = std::expected<void, vultra::Error>;

    void testSwapchainColor()
    {
        using namespace vultra;
        const auto window = must(Window::create("Swapchain color test", {160, 120}));
        const auto device = must(Device::create());
        const auto frame  = must(Frame::create(*device));
        for (const auto format : {VriFormat_BGRA8_UNORM, VriFormat_BGRA8_SRGB})
        {
            const auto swapchain = must(Swapchain::create(*device, *window, format));
            for (int pass = 0; pass < 2; ++pass)
            {
                window->setSize({uint32_t(160 + pass * 32), uint32_t(120 + pass * 24)});
                Texture* target = nullptr;
                for (int attempt = 0; attempt < 10 && !target; ++attempt)
                {
                    window->poll();
                    target = must(swapchain->acquire());
                }
                require(target != nullptr && target->desc.format == format,
                        "Swapchain did not retain its color format");
                auto* cmd = must(frame->begin());
                target->transition(cmd,
                                   {VriAccess_ColorAttachmentWrite,
                                    VriLayout_ColorAttachment,
                                    VriPipelineStage_ColorAttachmentOutput});
                const float clear[] {0.2f, 0.3f, 0.3f, 1};
                beginColorPass(*device, cmd, must(target->view()), swapchain->size(), clear);
                device->core.CmdEndRendering(cmd);
                must(frame->submitAndWait());
                const auto  pixels   = must(readback(*device, *target));
                const float expected = format == VriFormat_BGRA8_SRGB ? 124.0f / 255 : 0.2f;
                require(std::abs(pixels.rgba[0] - expected) < 0.005f,
                        "Swapchain clear color has an incorrect transfer");
                cmd = must(frame->begin());
                target->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
                must(frame->submitAndWait());
                must(swapchain->present());
            }
        }
    }

    class ViewportApp final : public vultra::ImGuiApp
    {
    public:
        static inline const vultra::AppConfig kConfig {"Vultra | Docking and Viewport Test", {400, 300}};

        explicit ViewportApp(Services services) :
            ImGuiApp(std::move(services), kConfig)
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
        Result onUpdate(float) override
        {
            if (frameCount() == 2 && !m_Minimized)
            {
                getWindow().minimize();
                m_Minimized = true;
            }
            if (getWindow().minimized() && ++m_MinimizedTicks == 3)
            {
                getWindow().restore();
            }
            const auto& viewports = ImGui::GetPlatformIO().Viewports;
            for (int i = 1; i < viewports.Size; ++i)
            {
                auto* viewport = viewports[i];
                if (viewport->RendererUserData && viewport->PlatformHandle)
                {
                    created         = true;
                    const auto size = ImGui::GetPlatformIO().Platform_GetWindowSize(viewport);
                    resized         = resized || (size.x == 320 && size.y == 220);
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
            return {};
        }

        Result onImGui() override
        {
            if (!m_Open)
            {
                closed = true;
                return {};
            }
            const bool minimized  = getWindow().minimized();
            minimizedGui          = minimizedGui || minimized;
            const auto origin     = ImGui::GetMainViewport()->Pos;
            const bool shouldDock = frameCount() >= 5 && frameCount() <= 9;
            ImGui::SetNextWindowDockID(shouldDock ? getEditorGui().dockspaceId() : 0, ImGuiCond_Always);
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
            return {};
        }

        Result onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
        {
            target.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[4] {0.1f, 0.2f, 0.3f, 1};
            vultra::beginColorPass(getDevice(), cmd, must(target.view()), getSwapchain().size(), clear);
            getDevice().core.CmdEndRendering(cmd);
            return drawGui(cmd, target);
        }

        bool     m_Open           = true;
        bool     m_Minimized      = false;
        uint32_t m_MinimizedTicks = 0;
    };

    void testGuiScale()
    {
        using namespace vultra;
        const auto window = must(Window::create("Vultra | ImGui Pixel Scale Test", {320, 240}));
        const auto device = must(Device::create());
        const auto frame  = must(Frame::create(*device));
        auto       target = must(Texture::create(*device, colorTexture({640, 480}, VriFormat_BGRA8_UNORM)));
        const auto gui    = must(EditorGui::create(*device,
                                                   *window,
                                                   target.desc.format,
                                                   {.docking = false, .multiViewport = false, .persistLayout = false}));
        must(gui->begin());
        auto* list = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
        list->PushClipRect({10, 10}, {20, 20});
        list->AddRectFilled({0, 0}, {60, 60}, IM_COL32(255, 0, 0, 255));
        list->PopClipRect();
        must(gui->upload({640, 480}));
        auto* cmd = must(frame->begin());
        gui->copy(cmd);
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float black[4] {0, 0, 0, 1};
        beginColorPass(*device, cmd, must(target.view()), {640, 480}, black);
        gui->draw(cmd);
        device->core.CmdEndRendering(cmd);
        must(frame->submitAndWait());
        const auto image = must(readback(*device, target));
        // The window's logical size includes the display content scale, so derive the expected pixels from it.
        const float scale  = 640.0f / float(window->size().width);
        const auto  sample = [&](float logical)
        {
            const auto pixel = size_t(logical * scale);
            return image.rgba[(pixel * 640 + pixel) * 4];
        };
        require(sample(15) > 0.99f, "GUI clipping did not scale with the framebuffer");
        require(sample(7) == 0 && sample(23) == 0, "GUI clipping leaked outside the scaled rectangle");
    }

    void testMirror()
    {
        using namespace vultra;
        const auto device = must(Device::create());
        const auto frame  = must(Frame::create(*device));
        auto       output = must(Texture::create(*device, colorTexture({128, 64})));
        const auto blit   = must(TextureBlit::create(*device, output.desc.format, 2));
        for (auto format : {VriFormat_RGBA8_SRGB, VriFormat_RGBA8_UNORM, VriFormat_BGRA8_SRGB, VriFormat_BGRA8_UNORM})
        {
            auto left  = must(Texture::create(*device, colorTexture({32, 32}, format)));
            auto right = must(Texture::create(*device, colorTexture({32, 32}, format)));
            must(blit->setSource(0, left));
            must(blit->setSource(1, right));
            auto*       cmd = must(frame->begin());
            const float leftColor[4] {0.18f, 0.04f, 0.5f, 1};
            const float rightColor[4] {0.5f, 0.18f, 0.04f, 1};
            for (auto* source : {&left, &right})
            {
                source->transition(cmd,
                                   {VriAccess_ColorAttachmentWrite,
                                    VriLayout_ColorAttachment,
                                    VriPipelineStage_ColorAttachmentOutput});
                beginColorPass(*device, cmd, must(source->view()), {32, 32}, source == &left ? leftColor : rightColor);
                device->core.CmdEndRendering(cmd);
            }
            output.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float black[4] {0, 0, 0, 1};
            beginColorPass(*device, cmd, must(output.view()), {128, 64}, black);
            device->core.CmdEndRendering(cmd);
            must(blit->draw(cmd, output, {8, 8, 48, 48}, 0, true));
            must(blit->draw(cmd, output, {72, 8, 48, 48}, 1, true));
            must(frame->submitAndWait());
            const auto image = must(readback(*device, output));
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
        // The C++ indexed triangle is the reference for shader-generated and XR paths.
        auto        reference = must(Texture::create(*device, colorTexture({128, 64}, VriFormat_RGBA32_SFLOAT)));
        const auto  indexed   = must(sample::ColoredMesh::create(*device,
                                                                 reference.desc.format,
                                                                 sample::kTriangleVertices,
                                                                 sample::kTriangleIndices));
        auto*       cmd       = must(frame->begin());
        const float black[4] {0, 0, 0, 1};
        reference.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        must(indexed->draw(cmd, reference, black));
        must(frame->submitAndWait());
        const auto expected = must(readback(*device, reference));
        const auto channel  = [&](uint32_t x, uint32_t y, uint32_t color)
        {
            return expected.rgba[(size_t(y) * expected.size.width + x) * 4 + color];
        };
        require(channel(64, 22, 0) > 0.5f && channel(40, 44, 1) > 0.5f && channel(88, 44, 2) > 0.5f,
                "VRI Y-up clip space must read back red above green and blue");
        auto compareTriangle = [&](const Image& image, bool linearUnorm = false)
        {
            for (size_t i = 0; i < expected.rgba.size(); ++i)
            {
                float channel = expected.rgba[i];
                if (i % 4 != 3)
                {
                    // Linear 8-bit XR attachments quantize before the mirror's sRGB encode.
                    if (linearUnorm)
                    {
                        channel = std::round(channel * 255) / 255;
                    }
                    channel =
                        channel <= 0.0031308f ? channel * 12.92f : 1.055f * std::pow(channel, 1.0f / 2.4f) - 0.055f;
                }
                require(std::abs(channel - image.rgba[i]) < 0.01f,
                        "Triangle geometry or display colors differ between rendering paths");
            }
        };
        for (auto format : {VriFormat_BGRA8_UNORM, VriFormat_BGRA8_SRGB})
        {
            auto       desktop  = must(Texture::create(*device, colorTexture({128, 64}, format)));
            const auto triangle = must(Triangle::create(*device, format, "examples/offline/shaders/triangle.slang"));
            cmd                 = must(frame->begin());
            desktop.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            must(triangle->draw(cmd, desktop, black));
            must(frame->submitAndWait());
            compareTriangle(must(readback(*device, desktop)));
        }
        for (auto format : {VriFormat_RGBA8_SRGB, VriFormat_RGBA8_UNORM, VriFormat_BGRA8_SRGB, VriFormat_BGRA8_UNORM})
        {
            auto       eye = must(Texture::create(*device, colorTexture({128, 64}, format)));
            const auto xr  = must(Triangle::create(*device, format, "examples/offline/shaders/triangle.slang"));
            xr->parameters.encodeSrgb = 0;
            must(blit->setSource(0, eye));
            cmd = must(frame->begin());
            eye.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            must(xr->draw(cmd, eye, black));
            must(blit->draw(cmd, output, {0, 0, 128, 64}, 0, true));
            must(frame->submitAndWait());
            compareTriangle(must(readback(*device, output)),
                            format == VriFormat_RGBA8_UNORM || format == VriFormat_BGRA8_UNORM);
        }
    }
} // namespace

int main()
{
    {
        ViewportApp app(must(vultra::ImGuiApp::createServices(ViewportApp::kConfig, {.persistLayout = false})));
        must(app.run(16));
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
