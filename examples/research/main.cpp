#include "../common/triangle.hpp"

#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/core/profiling/profiler.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/rendergraph/render_graph.hpp>
#include <vultra/function/research/capture.hpp>

#include <fstream>

using namespace vultra;

class ResearchApp final : public ImGuiApp
{
public:
    explicit ResearchApp(const argparse::ArgumentParser& cli) :
        ImGuiApp({"Vultra | Research - Shader Reload and Capture"}),
        m_DumpDirectory(cli.present<std::string>("--dump").value_or("")),
        m_CapturePath(cli.present<std::string>("--capture").value_or("")),
        m_ReferencePath(cli.present<std::string>("--compare").value_or("")),
        m_Triangle(getDevice(), getSwapchain().format(), "examples/research/shaders/triangle.slang"),
        m_Profiler(getDevice())
    {
        if (!m_DumpDirectory.empty())
        {
            std::filesystem::create_directories(m_DumpDirectory);
            if (std::filesystem::exists(m_DumpDirectory / "timings.csv"))
            {
                throw std::runtime_error("Use a fresh dump directory");
            }
            m_Timings.open(m_DumpDirectory / "timings.csv");
            if (!m_Timings)
            {
                throw std::runtime_error("Cannot create timings.csv");
            }
            m_Timings << "frame,pass,cpu_record_ms,gpu_ms\n";
        }
    }

    void saveResults()
    {
        if (m_Graph && (!m_CapturePath.empty() || !m_ReferencePath.empty()))
        {
            const auto image = readback(getDevice(), m_Graph->getTexture(m_Scene));
            if (!m_CapturePath.empty())
            {
                savePng(image, m_CapturePath);
            }
            if (!m_ReferencePath.empty())
            {
                const auto metrics = compare(loadPng(m_ReferencePath), image);
                Logger::app().info("PSNR {} dB, SSIM {}, MSE {}", metrics.psnr, metrics.ssim, metrics.mse);
            }
        }
        if (m_Timings.is_open())
        {
            m_Timings.flush();
            if (!m_Timings)
            {
                throw std::runtime_error("Writing timings.csv failed");
            }
        }
    }

private:
    void onUpdate(float) override
    {
        m_Triangle.pipeline->poll();
        m_Screenshot = false;
    }

    void onImGui() override
    {
        ImGui::Begin("Experiment");
        ImGui::TextUnformatted("Edit examples/research/shaders/triangle.slang or color.slangh");
        ImGui::Text("Shader generation: %llu", static_cast<unsigned long long>(m_Triangle.pipeline->generation()));
        ImGui::ColorEdit3("Tint", m_Triangle.parameters.tint);
        ImGui::ColorEdit3("Background", m_ClearColor);
        m_Screenshot = ImGui::Button("Save scene PNG");
        if (!m_Triangle.pipeline->diagnostics().empty())
        {
            ImGui::TextWrapped("%s", m_Triangle.pipeline->diagnostics().c_str());
        }
        if (!m_Status.empty())
        {
            ImGui::TextWrapped("%s", m_Status.c_str());
        }
        ImGui::SeparatorText("Previous frame - CPU recording / GPU execution");
        for (const auto& timing : m_Profiler.timings())
        {
            ImGui::Text("%s: %.3f / %.3f ms", timing.name.c_str(), timing.cpuMs, timing.gpuMs);
        }
        if (!m_Profiler.hasGpuTimings())
        {
            ImGui::TextUnformatted("GPU timestamps unavailable");
        }
        ImGui::End();
    }

    void onRender(VriCommandBuffer* cmd, Texture& target) override
    {
        // Rebuild only on resize. Graph-owned textures persist across frames.
        if (!m_Graph || m_GraphSize != getSwapchain().size())
        {
            m_Graph      = std::make_unique<RenderGraph>(getDevice());
            m_GraphSize  = getSwapchain().size();
            m_Scene      = m_Graph->createTexture("scene", colorTexture(m_GraphSize, getSwapchain().format()));
            m_Backbuffer = m_Graph->importResource("backbuffer", target, false);

            m_Graph->addPass("Triangle",
                             {{m_Scene, Usage::eColorWrite}},
                             [this](auto* cmd, auto& g)
                             {
                                 m_Triangle.draw(cmd, g.getTexture(m_Scene), m_ClearColor);
                             });

            m_Graph->addPass("Copy scene",
                             {{m_Scene, Usage::eCopySource}, {m_Backbuffer, Usage::eCopyDestination}},
                             [this](auto* cmd, auto& g)
                             {
                                 VriTextureCopyDesc copy {};
                                 copy.src.layerNum = 1;
                                 copy.dst.layerNum = 1;
                                 copy.src.aspect   = VriImageAspect_Color;
                                 copy.dst.aspect   = VriImageAspect_Color;
                                 getDevice().core.CmdCopyTexture(cmd,
                                                                 g.getTexture(m_Backbuffer).handle,
                                                                 g.getTexture(m_Scene).handle,
                                                                 &copy);
                             });

            m_Graph->addPass("ImGui",
                             {{m_Backbuffer, Usage::eColorReadWrite}},
                             [this](auto* cmd, auto& g)
                             {
                                 getGui().copy(cmd);
                                 beginColorPass(getDevice(), cmd, g.getTexture(m_Backbuffer).view(), m_GraphSize);
                                 getGui().draw(cmd);
                                 getDevice().core.CmdEndRendering(cmd);
                             });

            m_Graph->addPass(
                "Present",
                {{m_Backbuffer, Usage::ePresent}},
                [](auto*, auto&)
                {
                },
                true);

            m_Graph->exportResource(m_Scene); // make it observable for research readback
            m_Graph->compile();
        }

        m_Graph->bind(m_Backbuffer, target);
        m_Graph->execute(cmd, &m_Profiler);
    }

    void onPostRender(Texture&) override
    {
        m_Profiler.collect();

        if (m_Timings.is_open())
        {
            for (const auto& t : m_Profiler.timings())
            {
                m_Timings << frameCount() << ',' << t.name << ',' << t.cpuMs << ',' << t.gpuMs << '\n';
            }
        }

        if (m_Screenshot || !m_DumpDirectory.empty())
        {
            const auto image = readback(getDevice(), m_Graph->getTexture(m_Scene));
            if (!m_DumpDirectory.empty())
            {
                dumpFrame(image, m_DumpDirectory, frameCount());
            }
            if (m_Screenshot)
            {
                const auto directory = std::filesystem::path("captures") /
                                       std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                dumpFrame(image, directory, frameCount());
                m_Status = "Saved native-resolution scene in " + directory.string();
            }
        }
    }

    std::filesystem::path        m_DumpDirectory;
    std::filesystem::path        m_CapturePath;
    std::filesystem::path        m_ReferencePath;
    Triangle                     m_Triangle;
    Profiler                     m_Profiler;
    std::unique_ptr<RenderGraph> m_Graph;
    RenderGraph::Resource        m_Scene {};
    RenderGraph::Resource        m_Backbuffer {};
    Extent                       m_GraphSize {};
    float                        m_ClearColor[4] {0.035f, 0.045f, 0.065f, 1};
    std::string                  m_Status;
    std::ofstream                m_Timings;
    bool                         m_Screenshot = false;
};

int main(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("example-research", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Shader hot reload, image metrics and frame capture");
    vultra::addAppOptions(cli);
    cli.add_argument("--dump").help("Dump scene PNGs and timings.csv into a new directory");
    cli.add_argument("--capture").help("Save the final scene image");
    cli.add_argument("--compare").help("Compare the final scene against this PNG");
    if (!parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    ResearchApp app(cli);
    app.run(cli.present<uint64_t>("--frames").value_or(0));
    app.saveResults();
    Logger::app().info("Rendered {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    Logger::app().error("{}", error.what());
    return 1;
}
