#include "../common/colored_mesh.hpp"
#include "../common/sample.hpp"

#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

class GraphApp final : public vultra::DesktopApp
{
public:
    explicit GraphApp(const sample::Options& options) :
        vultra::DesktopApp(
            {.title = "Vultra | RenderGraph - Triangle", .size = {1024, 768}, .swapchainFormat = VriFormat_BGRA8_SRGB}),
        m_Options(options),
        m_Triangle(getDevice(), getSwapchain().format(), sample::kTriangleVertices, sample::kTriangleIndices),
        m_Profiler(getDevice())
    {
    }

private:
    void onUpdate(float) override
    {
        m_Triangle.pipeline->poll();
    }

    void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        if (!m_Graph || m_GraphSize != getSwapchain().size())
        {
            m_GraphSize = getSwapchain().size();
            m_Graph     = std::make_unique<vultra::RenderGraph>(getDevice());
            const auto scene =
                m_Graph->createTexture("scene", vultra::colorTexture(m_GraphSize, getSwapchain().format()));
            m_Backbuffer = m_Graph->importResource("backbuffer", target, false);
            m_Graph->addPass("Triangle",
                             {{scene, vultra::Usage::eColorWrite}},
                             [this, scene](auto* cmd, auto& resources)
                             {
                                 const float clear[4] {0, 0, 0, 1};
                                 m_Triangle.draw(cmd, resources.getTexture(scene), clear);
                             });
            m_Graph->addPass("Copy scene",
                             {{scene, vultra::Usage::eCopySource}, {m_Backbuffer, vultra::Usage::eCopyDestination}},
                             [this, scene](auto* cmd, auto& resources)
                             {
                                 VriTextureCopyDesc copy {};
                                 copy.src.layerNum = 1;
                                 copy.dst.layerNum = 1;
                                 copy.src.aspect   = VriImageAspect_Color;
                                 copy.dst.aspect   = VriImageAspect_Color;
                                 getDevice().core.CmdCopyTexture(cmd,
                                                                 resources.getTexture(m_Backbuffer).handle,
                                                                 resources.getTexture(scene).handle,
                                                                 &copy);
                             });
            m_Graph->addPass(
                "Present",
                {{m_Backbuffer, vultra::Usage::ePresent}},
                [](auto*, auto&)
                {
                },
                true);
            m_Graph->compile();
            for (const auto& pass : m_Graph->activePasses())
            {
                vultra::Logger::app().debug("Pass: {}", pass);
            }
        }
        m_Graph->bind(m_Backbuffer, target);
        m_Graph->execute(cmd, &m_Profiler);
    }

    void onPostRender(vultra::Texture& target) override
    {
        m_Profiler.collect();
        sample::captureFrame(m_Options, frameCount(), getDevice(), target);
    }

    sample::Options                      m_Options;
    sample::ColoredMesh                  m_Triangle;
    vultra::Profiler                     m_Profiler;
    std::unique_ptr<vultra::RenderGraph> m_Graph;
    vultra::RenderGraph::Resource        m_Backbuffer {};
    vultra::Extent                       m_GraphSize {};
};

int runRenderGraph(int argc, char** argv)
try
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return 0;
    }
    GraphApp app(*options);
    app.run(options->frames);
    vultra::Logger::app().info("RenderGraph triangle: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
