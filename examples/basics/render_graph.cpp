#include "../common/colored_mesh.hpp"
#include "../common/sample.hpp"
#include "../common/shader_pack.hpp"

#include <vultra/app/platform_app.hpp>
#include <vultra/gpu/profiling/profiler.hpp>
#include <vultra/graph/render_graph.hpp>

class GraphApp final : public vultra::PlatformApp
{
public:
    static std::expected<std::unique_ptr<GraphApp>, vultra::Error>
    create(Services services, const vultra::AppConfig& config, const sample::Options& options)
    {
        const auto program = sample::cookedShader(options.launch, "mesh.vshaderc");
        if (!program)
        {
            return std::unexpected(program.error());
        }
        auto triangle = sample::ColoredMesh::create(services.device(),
                                                    services.swapchain().format(),
                                                    sample::kTriangleVertices,
                                                    sample::kTriangleIndices,
                                                    VriPrimitiveTopology_TriangleList,
                                                    false,
                                                    *program ? &**program : nullptr);
        if (!triangle)
        {
            return std::unexpected(triangle.error());
        }
        auto profiler = vultra::Profiler::create(services.device());
        if (!profiler)
        {
            return std::unexpected(profiler.error());
        }
        return std::unique_ptr<GraphApp>(
            new GraphApp(std::move(services), config, options, std::move(*triangle), std::move(*profiler)));
    }

private:
    GraphApp(Services                             services,
             const vultra::AppConfig&             config,
             const sample::Options&               options,
             std::unique_ptr<sample::ColoredMesh> triangle,
             std::unique_ptr<vultra::Profiler>    profiler) :
        vultra::PlatformApp(std::move(services), config),
        m_Options(options),
        m_Triangle(std::move(triangle)),
        m_Profiler(std::move(profiler))
    {
    }

    std::expected<void, vultra::Error> onUpdate(float) override
    {
        m_Triangle->pipeline->poll();
        return {};
    }

    // Rebuilt only when the swapchain size changes; the backbuffer is rebound every frame.
    std::expected<void, vultra::Error> buildGraph(vultra::Texture& target)
    {
        m_GraphSize      = getSwapchain().size();
        m_Graph          = std::make_unique<vultra::RenderGraph>(getDevice());
        const auto scene = m_Graph->createTexture("scene", vultra::colorTexture(m_GraphSize, getSwapchain().format()));
        if (!scene)
        {
            return std::unexpected(scene.error());
        }
        const auto backbuffer = m_Graph->importResource("backbuffer", target, false);
        if (!backbuffer)
        {
            return std::unexpected(backbuffer.error());
        }
        m_Backbuffer = *backbuffer;
        if (auto added = m_Graph->addPass(
                "Triangle",
                {{*scene, vultra::Usage::eColorWrite}},
                [this, scene = *scene](auto* cmd, auto& resources) -> std::expected<void, vultra::Error>
                {
                    const float clear[4] {0, 0, 0, 1};
                    return m_Triangle->draw(cmd, resources.getTexture(scene), clear);
                });
            !added)
        {
            return added;
        }
        if (auto added = m_Graph->addPass(
                "Copy scene",
                {{*scene, vultra::Usage::eCopySource}, {m_Backbuffer, vultra::Usage::eCopyDestination}},
                [this, scene = *scene](auto* cmd, auto& resources) -> std::expected<void, vultra::Error>
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
                    return {};
                });
            !added)
        {
            return added;
        }
        if (auto added = m_Graph->addPass(
                "Present",
                {{m_Backbuffer, vultra::Usage::ePresent}},
                [](auto*, auto&) -> std::expected<void, vultra::Error>
                {
                    return {};
                },
                true);
            !added)
        {
            return added;
        }
        if (auto compiled = m_Graph->compile(); !compiled)
        {
            return compiled;
        }
        for (const auto& pass : m_Graph->activePasses())
        {
            vultra::Logger::app().debug("Pass: {}", pass);
        }
        return {};
    }

    std::expected<void, vultra::Error> onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        if (!m_Graph || m_GraphSize != getSwapchain().size())
        {
            if (auto built = buildGraph(target); !built)
            {
                return built;
            }
        }
        if (auto bound = m_Graph->bind(m_Backbuffer, target); !bound)
        {
            return bound;
        }
        return m_Graph->execute(cmd, m_Profiler.get());
    }

    std::expected<void, vultra::Error> onPostRender(vultra::Texture& target) override
    {
        if (auto collected = m_Profiler->collect(); !collected)
        {
            return collected;
        }
        return sample::captureFrame(m_Options, frameCount(), getDevice(), target);
    }

    sample::Options                      m_Options;
    std::unique_ptr<sample::ColoredMesh> m_Triangle;
    std::unique_ptr<vultra::Profiler>    m_Profiler;
    std::unique_ptr<vultra::RenderGraph> m_Graph;
    vultra::RenderGraph::Resource        m_Backbuffer {};
    vultra::Extent                       m_GraphSize {};
};

int runRenderGraph(const vultra::AppLaunch& launch, int argc, char** argv)
{
    const auto options = sample::readOptions(argc, argv, launch);
    if (!options)
    {
        return sample::fail(options.error());
    }
    if (!*options)
    {
        return 0;
    }
    const vultra::AppConfig config {.title           = "Vultra | RenderGraph - Triangle",
                                    .size            = {1024, 768},
                                    .swapchainFormat = VriFormat_BGRA8_SRGB};
    auto                    services = vultra::PlatformApp::createServices(config, launch);
    if (!services)
    {
        return sample::fail(services.error());
    }
    const auto app = GraphApp::create(std::move(*services), config, **options);
    if (!app)
    {
        return sample::fail(app.error());
    }
    if (auto result = (*app)->run((*options)->frames); !result)
    {
        return sample::fail(result.error());
    }
    vultra::Logger::app().info("RenderGraph triangle: {} frames", (*app)->frameCount());
    return 0;
}
