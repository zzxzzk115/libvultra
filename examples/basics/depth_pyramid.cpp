#include "../common/sample.hpp"
#include "../common/shader_pack.hpp"

#include <vultra/app/platform_app.hpp>
#include <vultra/graph/render_graph.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#include <array>
#include <bit>
#include <optional>
#include <string>

namespace
{
    constexpr const char* kShader = "examples/basics/depth_pyramid.slang";

    // Matches Parameters in depth_pyramid.slang.
    struct Parameters
    {
        uint32_t sourceSize[2] {};
        uint32_t targetSize[2] {};
        float    time   = 0;
        uint32_t levels = 0;
    };

    vultra::Extent levelSize(vultra::Extent size, uint32_t mip)
    {
        return {std::max(size.width >> mip, 1u), std::max(size.height >> mip, 1u)};
    }
} // namespace

// A depth buffer, its pyramid of 2x2 maxima built one level per pass, and a view of every level. Each reduction
// samples one mip of the pyramid and writes the next mip of the same texture.
class DepthPyramidApp final : public vultra::PlatformApp
{
public:
    static std::expected<std::unique_ptr<DepthPyramidApp>, vultra::Error>
    create(Services services, const vultra::AppConfig& config, const sample::Options& options)
    {
        auto program = sample::cookedShader(options.launch, "depth_pyramid.vshaderc");
        if (!program)
        {
            return std::unexpected(program.error());
        }
        std::unique_ptr<DepthPyramidApp> app(new DepthPyramidApp(std::move(services), config, options));
        if (auto built = app->buildPipelines(*program ? &**program : nullptr); !built)
        {
            return std::unexpected(built.error());
        }
        return app;
    }

    ~DepthPyramidApp() override
    {
        getDevice().waitIdleForDestruction();
        m_Graph.reset();
        m_Scene.reset();
        m_Copy.reset();
        m_Reduce.reset();
        m_Display.reset();
        if (m_Pool)
        {
            getDevice().core.DestroyDescriptorPool(m_Pool);
        }
        if (m_Layout)
        {
            getDevice().core.DestroyPipelineLayout(m_Layout);
        }
    }

    DepthPyramidApp(const DepthPyramidApp&)            = delete;
    DepthPyramidApp& operator=(const DepthPyramidApp&) = delete;

private:
    DepthPyramidApp(Services services, const vultra::AppConfig& config, const sample::Options& options) :
        vultra::PlatformApp(std::move(services), config),
        m_Options(options)
    {
    }

    // One layout serves every stage: binding 0 is read, binding 1 written, and the parameters are push constants.
    std::expected<void, vultra::Error> buildPipelines(const vultra::ShaderProgram* program)
    {
        auto&                        device = getDevice();
        constexpr auto               stages = VriShaderStage_Compute | VriShaderStage_Vertex | VriShaderStage_Fragment;
        const VriDescriptorRangeDesc ranges[2] {{0, 1, VriDescriptorType_Texture, stages},
                                                {1, 1, VriDescriptorType_StorageTexture, stages}};
        const VriDescriptorSetDesc   set {0, ranges, 2};
        const VriPushConstantDesc    constants {0, sizeof(Parameters), stages};
        VriPipelineLayoutDesc        layout {};
        layout.descriptorSets   = &set;
        layout.descriptorSetNum = 1;
        layout.pushConstants    = &constants;
        layout.pushConstantNum  = 1;
        layout.shaderStages     = stages;
        if (auto created = vultra::check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout),
                                         "Create depth pyramid layout");
            !created)
        {
            return created;
        }
        const auto compute =
            [this](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, vultra::Error>
        {
            VriPipeline*                 result = nullptr;
            const VriComputePipelineDesc desc {m_Layout, shaders[0], getDevice().pipelineCache};
            if (auto created = vultra::check(getDevice().core.CreateComputePipeline(getDevice().handle, &desc, &result),
                                             "Create depth pyramid compute pipeline");
                !created)
            {
                return std::unexpected(created.error());
            }
            return result;
        };
        // The scene writes depth only; the display writes the backbuffer.
        const auto graphics = [this](VriFormat color)
        {
            return [this, color](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, vultra::Error>
            {
                VriColorAttachmentDesc attachment {};
                attachment.format         = color;
                attachment.colorWriteMask = VriColorWrite_RGBA;
                VriGraphicsPipelineDesc desc {};
                desc.pipelineCache           = getDevice().pipelineCache;
                desc.pipelineLayout          = m_Layout;
                desc.shaders                 = shaders.data();
                desc.shaderNum               = uint32_t(shaders.size());
                desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                desc.rasterization.cullMode  = VriCullMode_None;
                desc.rasterization.lineWidth = 1;
                desc.multisample.sampleNum   = 1;
                if (color == VriFormat_D32_SFLOAT)
                {
                    desc.depthStencil.depthTest          = VRI_TRUE;
                    desc.depthStencil.depthWrite         = VRI_TRUE;
                    desc.depthStencil.depthCompareOp     = VriCompareOp_Always;
                    desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
                }
                else
                {
                    desc.outputMerger.colors   = &attachment;
                    desc.outputMerger.colorNum = 1;
                }
                VriPipeline* result = nullptr;
                if (auto created =
                        vultra::check(getDevice().core.CreateGraphicsPipeline(getDevice().handle, &desc, &result),
                                      "Create depth pyramid graphics pipeline");
                    !created)
                {
                    return std::unexpected(created.error());
                }
                return result;
            };
        };
        const auto create = [&](std::vector<vultra::ShaderEntry> entries, vultra::ShaderPipeline::Builder builder)
        {
            return program ? vultra::ShaderPipeline::create(device, *program, std::move(entries), std::move(builder)) :
                             vultra::ShaderPipeline::create(device, kShader, std::move(entries), std::move(builder));
        };
        auto scene   = create({{"fullscreen", VriShaderStage_Vertex}, {"scene", VriShaderStage_Fragment}},
                            graphics(VriFormat_D32_SFLOAT));
        auto copy    = create({{"copyDepth", VriShaderStage_Compute}}, compute);
        auto reduce  = create({{"reduce", VriShaderStage_Compute}}, compute);
        auto display = create({{"fullscreen", VriShaderStage_Vertex}, {"display", VriShaderStage_Fragment}},
                              graphics(getSwapchain().format()));
        for (const auto* pipeline : {&scene, &copy, &reduce, &display})
        {
            if (!*pipeline)
            {
                return std::unexpected(pipeline->error());
            }
        }
        m_Scene   = std::move(*scene);
        m_Copy    = std::move(*copy);
        m_Reduce  = std::move(*reduce);
        m_Display = std::move(*display);
        return {};
    }

    std::expected<void, vultra::Error> onUpdate(float) override
    {
        for (const auto* pipeline : {&m_Scene, &m_Copy, &m_Reduce, &m_Display})
        {
            (*pipeline)->poll();
        }
        return {};
    }

    // Binds the pipeline and then a set of its own: VRI binds a set where the bound pipeline runs, and a set
    // must not change between recording and the end of the frame. A null set index binds no set.
    void bind(VriCommandBuffer*       cmd,
              vultra::ShaderPipeline& pipeline,
              std::optional<size_t>   index  = std::nullopt,
              VriDescriptor*          source = nullptr,
              VriDescriptor*          target = nullptr)
    {
        auto& device = getDevice();
        device.core.CmdSetPipelineLayout(cmd, m_Layout);
        device.core.CmdSetPipeline(cmd, pipeline.handle());
        if (!index)
        {
            return;
        }
        const VriDescriptor*               views[2] {source, target};
        const VriDescriptorRangeUpdateDesc updates[2] {{&views[0], 1}, {&views[1], 1}};
        auto*                              set = m_Sets[*index];
        device.core.UpdateDescriptorRanges(set, 0, target ? 2 : 1, updates);
        device.core.CmdSetDescriptorSet(cmd, 0, set);
    }

    std::expected<void, vultra::Error> dispatch(VriCommandBuffer* cmd, const Parameters& parameters)
    {
        auto& device = getDevice();
        device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
        const VriDispatchDesc groups {(parameters.targetSize[0] + 7) / 8, (parameters.targetSize[1] + 7) / 8, 1};
        device.core.CmdDispatch(cmd, &groups);
        return {};
    }

    void drawFullscreen(VriCommandBuffer* cmd, const Parameters& parameters)
    {
        auto&             device = getDevice();
        const VriViewport viewport {0, 0, float(m_Size.width), float(m_Size.height), 0, 1};
        const VriRect     scissor {0, 0, m_Size.width, m_Size.height};
        device.core.CmdSetViewports(cmd, &viewport, 1);
        device.core.CmdSetScissors(cmd, &scissor, 1);
        device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
        const VriDrawDesc draw {3, 1, 0, 0};
        device.core.CmdDraw(cmd, &draw);
        device.core.CmdEndRendering(cmd);
    }

    // Rebuilt only when the swapchain size changes; the backbuffer is rebound every frame.
    std::expected<void, vultra::Error> buildGraph(vultra::Texture& target)
    {
        using vultra::Usage;
        auto& device = getDevice();
        m_Size       = getSwapchain().size();
        m_Levels     = uint32_t(std::bit_width(std::max(m_Size.width, m_Size.height)));
        if (m_Graph)
        {
            device.waitIdleForDestruction();
            m_Graph.reset();
            device.core.DestroyDescriptorPool(m_Pool);
            m_Pool = nullptr;
        }
        // Set 0 copies the depth, set N reduces into level N and the last one displays the pyramid.
        m_Sets.resize(m_Levels + 1);
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum  = uint32_t(m_Sets.size());
        pool.textureMaxNum        = uint32_t(m_Sets.size());
        pool.storageTextureMaxNum = uint32_t(m_Sets.size());
        if (auto created = vultra::check(device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool),
                                         "Create depth pyramid pool");
            !created)
        {
            return created;
        }
        if (auto allocated = vultra::check(
                device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, m_Sets.data(), uint32_t(m_Sets.size())),
                "Allocate depth pyramid sets");
            !allocated)
        {
            return allocated;
        }

        m_Graph         = std::make_unique<vultra::RenderGraph>(device);
        auto& graph     = *m_Graph;
        auto  depthDesc = vultra::depthTexture(m_Size);
        auto  hzbDesc   = vultra::colorTexture(m_Size, VriFormat_R32_SFLOAT);
        hzbDesc.mipNum  = m_Levels;
        hzbDesc.usage =
            VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ShaderResource | VriTextureUsage_TransferSrc;
        const auto depth      = graph.createTexture("scene.depth", depthDesc);
        const auto hzb        = graph.createTexture("depth.hzb", hzbDesc);
        const auto backbuffer = graph.importResource("backbuffer", target, false);
        for (const auto* resource : {&depth, &hzb, &backbuffer})
        {
            if (!*resource)
            {
                return std::unexpected(resource->error());
            }
        }
        m_Backbuffer = *backbuffer;
        std::vector<std::expected<void, vultra::Error>> added;
        added.push_back(graph.addPass("Scene depth",
                                      {{*depth, Usage::eDepthWrite, true}},
                                      [this, depth = *depth](VriCommandBuffer* cmd, vultra::RenderGraph& resources)
                                          -> std::expected<void, vultra::Error>
                                      {
                                          const auto view = resources.getTexture(depth).view();
                                          if (!view)
                                          {
                                              return std::unexpected(view.error());
                                          }
                                          VriAttachmentDesc attachment {};
                                          attachment.view                          = *view;
                                          attachment.loadOp                        = VriAttachmentLoadOp_Clear;
                                          attachment.storeOp                       = VriAttachmentStoreOp_Store;
                                          attachment.clearValue.depthStencil.depth = 1;
                                          VriAttachmentsDesc attachments {};
                                          attachments.depth      = &attachment;
                                          attachments.renderArea = {0, 0, m_Size.width, m_Size.height};
                                          attachments.layerNum   = 1;
                                          getDevice().core.CmdBeginRendering(cmd, &attachments);
                                          bind(cmd, *m_Scene);
                                          drawFullscreen(cmd, {{}, {}, m_Time, 0});
                                          return {};
                                      }));
        added.push_back(graph.addPass(
            "HZB 0",
            {{*depth, Usage::eSampled}, {*hzb, Usage::eStorageWrite, true, {.mip = 0}}},
            [this, depth = *depth, hzb = *hzb](VriCommandBuffer*    cmd,
                                               vultra::RenderGraph& resources) -> std::expected<void, vultra::Error>
            {
                const auto source = resources.getTexture(depth).view();
                const auto target = resources.getTexture(hzb).view({.mip = 0});
                if (!source || !target)
                {
                    return std::unexpected(source ? target.error() : source.error());
                }
                bind(cmd, *m_Copy, 0, *source, *target);
                return dispatch(cmd, {{}, {m_Size.width, m_Size.height}, 0, 0});
            }));
        for (uint32_t mip = 1; mip < m_Levels; ++mip)
        {
            added.push_back(graph.addPass(
                "HZB " + std::to_string(mip),
                {{*hzb, Usage::eSampled, false, {.mip = mip - 1}}, {*hzb, Usage::eStorageWrite, true, {.mip = mip}}},
                [this, hzb = *hzb, mip](VriCommandBuffer*    cmd,
                                        vultra::RenderGraph& resources) -> std::expected<void, vultra::Error>
                {
                    auto&      texture = resources.getTexture(hzb);
                    const auto source  = texture.view({.mip = mip - 1});
                    const auto target  = texture.view({.mip = mip});
                    if (!source || !target)
                    {
                        return std::unexpected(source ? target.error() : source.error());
                    }
                    bind(cmd, *m_Reduce, mip, *source, *target);
                    const auto above = levelSize(m_Size, mip - 1);
                    const auto below = levelSize(m_Size, mip);
                    return dispatch(cmd, {{above.width, above.height}, {below.width, below.height}, 0, 0});
                }));
        }
        added.push_back(graph.addPass("Display",
                                      {{*hzb, Usage::eSampled}, {m_Backbuffer, Usage::eColorWrite, true}},
                                      [this, hzb = *hzb](VriCommandBuffer* cmd, vultra::RenderGraph& resources)
                                          -> std::expected<void, vultra::Error>
                                      {
                                          const auto source = resources.getTexture(hzb).view();
                                          const auto target = resources.getTexture(m_Backbuffer).view();
                                          if (!source || !target)
                                          {
                                              return std::unexpected(source ? target.error() : source.error());
                                          }
                                          // The pass replaces the backbuffer, so the attachment is cleared rather than
                                          // loaded.
                                          const float clear[4] {0, 0, 0, 1};
                                          vultra::beginColorPass(getDevice(), cmd, *target, m_Size, clear);
                                          bind(cmd, *m_Display, m_Levels, *source);
                                          drawFullscreen(cmd, {{m_Size.width, m_Size.height}, {}, 0, m_Levels});
                                          return {};
                                      }));
        added.push_back(graph.addPass(
            "Present",
            {{m_Backbuffer, Usage::ePresent}},
            [](VriCommandBuffer*, vultra::RenderGraph&) -> std::expected<void, vultra::Error>
            {
                return {};
            },
            true));
        for (const auto& result : added)
        {
            if (!result)
            {
                return result;
            }
        }
        return graph.compile();
    }

    std::expected<void, vultra::Error> onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        if (!m_Graph || m_Size != getSwapchain().size())
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
        // A fixed step per frame keeps captures reproducible.
        m_Time = float(frameCount()) / 60;
        return m_Graph->execute(cmd);
    }

    std::expected<void, vultra::Error> onPostRender(vultra::Texture& target) override
    {
        return sample::captureFrame(m_Options, frameCount(), getDevice(), target);
    }

    sample::Options                         m_Options;
    VriPipelineLayout*                      m_Layout = nullptr;
    VriDescriptorPool*                      m_Pool   = nullptr;
    std::vector<VriDescriptorSet*>          m_Sets;
    std::unique_ptr<vultra::ShaderPipeline> m_Scene;
    std::unique_ptr<vultra::ShaderPipeline> m_Copy;
    std::unique_ptr<vultra::ShaderPipeline> m_Reduce;
    std::unique_ptr<vultra::ShaderPipeline> m_Display;
    std::unique_ptr<vultra::RenderGraph>    m_Graph;
    vultra::RenderGraph::Resource           m_Backbuffer {};
    vultra::Extent                          m_Size {};
    uint32_t                                m_Levels = 0;
    float                                   m_Time   = 0;
};

int runDepthPyramid(const vultra::AppLaunch& launch, int argc, char** argv)
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
    const vultra::AppConfig config {.title           = "Vultra | RenderGraph - Depth pyramid",
                                    .size            = {1024, 768},
                                    .swapchainFormat = VriFormat_BGRA8_SRGB};
    auto                    services = vultra::PlatformApp::createServices(config, launch);
    if (!services)
    {
        return sample::fail(services.error());
    }
    const auto app = DepthPyramidApp::create(std::move(*services), config, **options);
    if (!app)
    {
        return sample::fail(app.error());
    }
    if (auto result = (*app)->run((*options)->frames); !result)
    {
        return sample::fail(result.error());
    }
    vultra::Logger::app().info("Depth pyramid: {} frames", (*app)->frameCount());
    return 0;
}
