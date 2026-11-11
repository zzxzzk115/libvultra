#include "support/check.hpp"

#include <vultra/app/imgui_app.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/image_readback.hpp>

#include <cmath>
#include <string>

namespace
{
    using support::must;
    using support::require;

    using Result = std::expected<void, vultra::Error>;

    void frameStatistics()
    {
        vultra::FrameStatistics stats;
        require(stats.addFrame(0.1, 10, 2) == " | FPS: 10.0 | CPU: 10.00 ms | GPU: 2.00 ms",
                "First frame statistics are missing or mislabeled");
        require(!stats.addFrame(0.2, 20, 4), "Frame statistics refresh too frequently");
        require(stats.addFrame(0.3, 40, 8) == " | FPS: 4.0 | CPU: 30.00 ms | GPU: 6.00 ms",
                "FPS must use total elapsed time; CPU/GPU must average completed frames");
        require(stats.addFrame(0.5, 1, std::nullopt) == " | FPS: 2.0 | CPU: 1.00 ms | GPU: N/A",
                "Missing GPU timestamps must not be shown as zero");
    }

    class TestApp final : public vultra::ImGuiApp
    {
    public:
        static inline const vultra::AppConfig kConfig {"Vultra - application test", {320, 240}};

        explicit TestApp(Services services) :
            ImGuiApp(std::move(services), kConfig)
        {
        }

        bool     resized        = false;
        uint64_t completed      = 0;
        uint64_t guiFrames      = 0;
        uint64_t minimizedTicks = 0;

    private:
        Result onResize(vultra::Extent size) override
        {
            require(m_PostUpdated, "Resize happened before the update phases finished");
            require(size == getSwapchain().size(), "Resize callback extent mismatch");
            // setSize() takes logical units; the swapchain extent is in pixels and differs on high-density displays.
            resized = resized || getWindow().size() == vultra::Extent {480, 320};
            return {};
        }

        Result onPreUpdate(float) override
        {
            m_Updated     = false;
            m_PostUpdated = false;
            return {};
        }

        Result onUpdate(float seconds) override
        {
            require(std::isfinite(seconds) && seconds >= 0, "Invalid frame delta");
            require(completed == frameCount(), "Update happened before previous frame completion");
            require(guiFrames == completed, "GUI frame started during logic update");
            m_Updated = true;
            if (getWindow().minimized())
            {
                ++minimizedTicks;
                if (minimizedTicks == 2)
                {
                    getWindow().restore();
                }
            }
            return {};
        }

        Result onPostUpdate(float) override
        {
            require(m_Updated, "PostUpdate happened before Update");
            m_PostUpdated = true;
            return {};
        }

        Result onPreRender() override
        {
            require(m_PostUpdated, "Render preparation happened before PostUpdate");
            require(!getWindow().minimized(), "Rendering while minimized");
            if (auto result = ImGuiApp::onPreRender(); !result)
            {
                return result;
            }
            require(ImGui::GetDrawData() && ImGui::GetDrawData()->Valid, "GUI draw data is not finalized");
            return {};
        }

        Result onImGui() override
        {
            require(m_PostUpdated, "GUI widgets were built before logic update finished");
            ++guiFrames;
            const auto origin = ImGui::GetMainViewport()->Pos;
            ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
                ->AddRectFilled({origin.x + 8, origin.y + 8}, {origin.x + 32, origin.y + 32}, IM_COL32(255, 0, 0, 255));
            if (resized && minimizedTicks >= 2 && frameCount() >= 2)
            {
                close(); // The acquired frame must still render, complete and present.
            }
            return {};
        }

        Result onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
        {
            require(guiFrames == frameCount() + 1, "Render did not receive exactly one GUI frame");
            target.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[4] {0.2f, 0.4f, 0.6f, 1};
            vultra::beginColorPass(getDevice(), cmd, must(target.view()), getSwapchain().size(), clear);
            getDevice().core.CmdEndRendering(cmd);
            return drawGui(cmd, target);
        }

        Result onPostRender(vultra::Texture& target) override
        {
            const auto image = must(vultra::readback(getDevice(), target));
            require(std::abs(image.rgba[0] - 0.2f) < 0.005f, "Completion hook cannot read rendered image");
            require(image.size.width == getSwapchain().size().width, "Readback has stale extent");
            const auto pixel = (16 * image.size.width + 16) * 4;
            if (image.rgba[pixel] < 0.99f)
            {
                must(vultra::savePng(image, "build/.tmp/desktop_gui_failure.png"));
                vultra::Logger::app().info("GUI frame {}: vertices {}, origin {}, {}; pixel {}, {}",
                                           frameCount(),
                                           ImGui::GetDrawData()->TotalVtxCount,
                                           ImGui::GetDrawData()->DisplayPos.x,
                                           ImGui::GetDrawData()->DisplayPos.y,
                                           image.rgba[pixel],
                                           image.rgba[pixel + 1]);
            }
            require(image.rgba[pixel] > 0.99f && image.rgba[pixel + 1] < 0.01f,
                    "GUI overlay was not rendered in the current frame");
            ++completed;
            if (frameCount() == 0)
            {
                getWindow().setSize({480, 320});
            }
            if (frameCount() == 1)
            {
                getWindow().minimize();
            }
            return {};
        }

        bool m_Updated     = false;
        bool m_PostUpdated = false;
    };

    class CloseDuringUpdateApp final : public vultra::DesktopApp
    {
    public:
        static inline const vultra::AppConfig kConfig {"Vultra - update close test", {160, 120}};

        explicit CloseDuringUpdateApp(Services services) :
            DesktopApp(std::move(services), kConfig)
        {
        }

        bool postUpdated = false;

    private:
        Result onUpdate(float) override
        {
            close();
            return {};
        }

        Result onPostUpdate(float) override
        {
            postUpdated = true;
            return {};
        }

        Result onRender(VriCommandBuffer*, vultra::Texture&) override
        {
            return vultra::makeError("close() during update must stop before acquiring/rendering a frame");
        }
    };
} // namespace

int main()
{
    frameStatistics();
    {
        TestApp app(must(vultra::ImGuiApp::createServices(TestApp::kConfig, {.persistLayout = false})));
        must(app.run(20));
        require(app.resized, "DesktopApp did not report window resize");
        require(app.minimizedTicks >= 2, "Logic updates stopped while the window was minimized");
        require(app.frameCount() >= 3 && app.frameCount() < 20, "close() did not stop the application");
        require(app.frameCount() == app.completed && app.completed == app.guiFrames,
                "Frame counter differs from completed render/GUI frames");
        const std::string title = app.getWindow().title();
        const auto        stats = title.find(" | FPS: ");
        require(title.starts_with("Vultra - application test") && stats != std::string::npos &&
                    title.find(" | CPU: ") != std::string::npos && title.find(" | GPU: ") != std::string::npos,
                "Desktop title did not retain the example name and frame statistics");
        app.getWindow().setTitle("Vultra - renamed model");
        require(app.getWindow().title() == "Vultra - renamed model" + title.substr(stats),
                "Changing model title lost or duplicated the statistics suffix");
    }
    {
        CloseDuringUpdateApp app(must(vultra::DesktopApp::createServices(CloseDuringUpdateApp::kConfig)));
        must(app.run(1));
        require(app.postUpdated && app.frameCount() == 0, "Update close did not finish updates and skip rendering");
    }
    vultra::Logger::app().info("Desktop tests passed: update/render phases, ImGui, readback, minimize/restore, resize, "
                               "close and frame statistics titles");
    return 0;
}
