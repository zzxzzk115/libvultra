#include "support/check.hpp"

#include <vultra/gpu/image_readback.hpp>
#include <vultra/graph/render_graph.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

namespace
{
    using namespace vultra;
    using support::must;
    using support::require;

    constexpr Extent kSize {16, 16};

    // Two compute kernels over one layout: binding 0 is a sampled input, binding 1 the storage output.
    class Kernels
    {
    public:
        Kernels(Device& device, const std::filesystem::path& directory) :
            m_Device(device)
        {
            const auto file = directory / "graph_async.slang";
            std::ofstream(file) << R"(
[[vk::binding(0, 0)]] Texture2D<float4>   source;
[[vk::binding(1, 0)]] RWTexture2D<float4> result;
[shader("compute")] [numthreads(8, 8, 1)]
void fill(uint3 id : SV_DispatchThreadID)
{
    result[id.xy] = float4(id.x / 15.0, id.y / 15.0, 0.25, 1);
}
[shader("compute")] [numthreads(8, 8, 1)]
void invert(uint3 id : SV_DispatchThreadID)
{
    result[id.xy] = float4(1 - source[id.xy].rgb, 1);
}
)";
            const VriDescriptorRangeDesc ranges[2] {{0, 1, VriDescriptorType_Texture, VriShaderStage_Compute},
                                                    {1, 1, VriDescriptorType_StorageTexture, VriShaderStage_Compute}};
            const VriDescriptorSetDesc   set {0, ranges, 2};
            VriPipelineLayoutDesc        layout {};
            layout.descriptorSets   = &set;
            layout.descriptorSetNum = 1;
            layout.shaderStages     = VriShaderStage_Compute;
            must(check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout), "Async test layout"));
            VriDescriptorPoolDesc pool {};
            pool.descriptorSetMaxNum  = uint32_t(m_Sets.size());
            pool.textureMaxNum        = uint32_t(m_Sets.size());
            pool.storageTextureMaxNum = uint32_t(m_Sets.size());
            must(check(device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool), "Async test pool"));
            must(check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, m_Sets.data(), uint32_t(m_Sets.size())),
                       "Async test sets"));
            const std::array names {"fill", "invert"};
            for (size_t index = 0; index < names.size(); ++index)
            {
                m_Pipelines[index] = must(ShaderPipeline::create(
                    device,
                    file,
                    std::vector<ShaderEntry> {{names[index], VriShaderStage_Compute}},
                    [this](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, Error>
                    {
                        VriPipeline*                 result = nullptr;
                        const VriComputePipelineDesc desc {m_Layout, shaders[0], m_Device.pipelineCache};
                        if (auto created = check(m_Device.core.CreateComputePipeline(m_Device.handle, &desc, &result),
                                                 "Async test pipeline");
                            !created)
                        {
                            return std::unexpected(created.error());
                        }
                        return result;
                    }));
            }
        }

        ~Kernels()
        {
            m_Device.waitIdleForDestruction();
            for (auto& pipeline : m_Pipelines)
            {
                pipeline.reset();
            }
            m_Device.core.DestroyDescriptorPool(m_Pool);
            m_Device.core.DestroyPipelineLayout(m_Layout);
        }

        // An unused input still needs a valid view; the output doubles as one for the fill kernel.
        std::expected<void, Error> dispatch(VriCommandBuffer* cmd, size_t kernel, Texture* input, Texture& output)
        {
            const auto inputView = input ? input->view() : m_Placeholder->view();
            if (!inputView)
            {
                return std::unexpected(inputView.error());
            }
            const auto outputView = output.view();
            if (!outputView)
            {
                return std::unexpected(outputView.error());
            }
            const VriDescriptor*               views[2] {*inputView, *outputView};
            const VriDescriptorRangeUpdateDesc updates[2] {{&views[0], 1}, {&views[1], 1}};
            m_Device.core.UpdateDescriptorRanges(m_Sets[kernel], 0, 2, updates);
            m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
            m_Device.core.CmdSetPipeline(cmd, m_Pipelines[kernel]->handle());
            m_Device.core.CmdSetDescriptorSet(cmd, 0, m_Sets[kernel]);
            const VriDispatchDesc groups {kSize.width / 8, kSize.height / 8, 1};
            m_Device.core.CmdDispatch(cmd, &groups);
            return {};
        }

        void setPlaceholder(Texture& texture)
        {
            m_Placeholder = &texture;
        }

    private:
        Device&                                        m_Device;
        VriPipelineLayout*                             m_Layout = nullptr;
        VriDescriptorPool*                             m_Pool   = nullptr;
        std::array<VriDescriptorSet*, 2>               m_Sets {};
        std::array<std::unique_ptr<ShaderPipeline>, 2> m_Pipelines;
        Texture*                                       m_Placeholder = nullptr;
    };

    VriTextureDesc imageDesc(uint32_t usage)
    {
        auto desc  = colorTexture(kSize, VriFormat_RGBA16_SFLOAT);
        desc.usage = usage;
        return desc;
    }

    void copyPass(RenderGraph& graph, RenderGraph::Resource from, RenderGraph::Resource to)
    {
        must(graph.addPass("Copy to output",
                           {{from, Usage::eCopySource}, {to, Usage::eCopyDestination, true}},
                           [from, to](VriCommandBuffer* cmd, RenderGraph& resources) -> std::expected<void, Error>
                           {
                               VriTextureCopyDesc copy {};
                               copy.src.aspect   = VriImageAspect_Color;
                               copy.dst.aspect   = VriImageAspect_Color;
                               copy.src.layerNum = 1;
                               copy.dst.layerNum = 1;
                               resources.device().core.CmdCopyTexture(cmd,
                                                                      resources.getTexture(to).handle,
                                                                      resources.getTexture(from).handle,
                                                                      &copy);
                               return {};
                           }));
    }

    struct Result
    {
        Image                 image;
        RenderGraph::Snapshot plan;
        std::string           dot;
    };

    // Compute fills an image on the async queue; a graphics-queue pass copies it out.
    Result computeThenGraphics(Device& device, Kernels& kernels, Texture& placeholder, bool async)
    {
        RenderGraph graph(device);
        const auto  filled =
            must(graph.createTexture("filled",
                                     imageDesc(VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ShaderResource |
                                               VriTextureUsage_TransferSrc)));
        const auto output =
            must(graph.createTexture("output", imageDesc(VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc)));
        must(graph.addPass("Fill",
                           {{filled, Usage::eStorageWrite, true}},
                           [&kernels, filled](VriCommandBuffer* cmd, RenderGraph& resources)
                           {
                               return kernels.dispatch(cmd, 0, nullptr, resources.getTexture(filled));
                           },
                           {.queue = PassQueue::eAsyncCompute}));
        copyPass(graph, filled, output);
        must(graph.exportResource(output));
        must(graph.compile(false, async));
        kernels.setPlaceholder(placeholder);
        const auto frame = must(Frame::create(device));
        // Two frames: the second starts from the states the first one left behind.
        for (int iteration = 0; iteration < 2; ++iteration)
        {
            must(frame->begin());
            must(graph.execute(*frame));
            must(frame->submitAndWait());
        }
        std::ostringstream dot;
        must(graph.writeDot(dot));
        return {must(readback(device, graph.getTexture(output))), must(graph.snapshot()), dot.str()};
    }

    // A graphics pass renders an image, async compute inverts it, and a graphics pass copies the result:
    // the compute batch sits between two graphics submissions.
    Result graphicsComputeGraphics(Device& device, Kernels& kernels, bool async)
    {
        RenderGraph graph(device);
        const auto  cleared = must(graph.createTexture("cleared", colorTexture(kSize, VriFormat_RGBA16_SFLOAT)));
        const auto  inverted =
            must(graph.createTexture("inverted",
                                     imageDesc(VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ShaderResource |
                                               VriTextureUsage_TransferSrc)));
        const auto output =
            must(graph.createTexture("output", imageDesc(VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc)));
        must(graph.addPass("Clear",
                           {{cleared, Usage::eColorWrite, true}},
                           [cleared](VriCommandBuffer* cmd, RenderGraph& resources) -> std::expected<void, Error>
                           {
                               const auto view = resources.getTexture(cleared).view();
                               if (!view)
                               {
                                   return std::unexpected(view.error());
                               }
                               const float color[4] {0.75f, 0.5f, 0.125f, 1};
                               beginColorPass(resources.device(), cmd, *view, kSize, color);
                               resources.device().core.CmdEndRendering(cmd);
                               return {};
                           }));
        must(graph.addPass(
            "Invert",
            {{cleared, Usage::eSampled}, {inverted, Usage::eStorageWrite, true}},
            [&kernels, cleared, inverted](VriCommandBuffer* cmd, RenderGraph& resources)
            {
                return kernels.dispatch(cmd, 1, &resources.getTexture(cleared), resources.getTexture(inverted));
            },
            {.queue = PassQueue::eAsyncCompute}));
        copyPass(graph, inverted, output);
        must(graph.exportResource(output));
        must(graph.compile(false, async));
        const auto frame = must(Frame::create(device));
        for (int iteration = 0; iteration < 2; ++iteration)
        {
            must(frame->begin());
            must(graph.execute(*frame));
            must(frame->submitAndWait());
        }
        if (must(graph.snapshot()).batches.size() > 1)
        {
            const auto recorded = graph.execute(must(frame->begin()));
            must(frame->submitAndWait());
            support::rejects(recorded, "A multi-queue plan was recorded into a single command buffer");
        }
        std::ostringstream dot;
        must(graph.writeDot(dot));
        return {must(readback(device, graph.getTexture(output))), must(graph.snapshot()), dot.str()};
    }

    void attachmentRejected(Device& device)
    {
        RenderGraph graph(device);
        const auto  target = must(graph.createTexture("target", colorTexture(kSize)));
        must(graph.addPass("Compute attachment",
                           {{target, Usage::eColorWrite, true}},
                           [](VriCommandBuffer*, RenderGraph&) -> std::expected<void, Error>
                           {
                               return {};
                           },
                           {.queue = PassQueue::eAsyncCompute}));
        must(graph.exportResource(target));
        support::rejects(graph.compile(), "An async compute pass with an attachment usage compiled");
    }
} // namespace

int main()
{
    const auto directory = std::filesystem::path("build/.tmp/graph-async") /
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(directory);
    const auto device = must(Device::create());
    Kernels    kernels(*device, directory);
    auto       placeholder =
        must(Texture::create(*device, imageDesc(VriTextureUsage_ShaderResource | VriTextureUsage_ConcurrentQueues)));
    {
        // The placeholder is sampled by nothing, but its view must be in a readable layout.
        const auto frame = must(Frame::create(*device));
        placeholder.transition(must(frame->begin()),
                               {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands});
        must(frame->submitAndWait());
    }
    attachmentRejected(*device);

    const bool dualQueue = device->computeQueue != nullptr;
    const auto single    = computeThenGraphics(*device, kernels, placeholder, false);
    const auto async     = computeThenGraphics(*device, kernels, placeholder, true);
    require(single.plan.batches.size() == 1 && single.plan.batches[0].queue == PassQueue::eGraphics,
            "A plan without async compute must be one graphics batch");
    // RGBA16F cannot represent these fractions exactly.
    require(std::abs(single.image.rgba[(5 * 16 + 9) * 4] - 9 / 15.0f) < 1e-3f &&
                std::abs(single.image.rgba[(5 * 16 + 9) * 4 + 1] - 5 / 15.0f) < 1e-3f,
            "Compute fill did not reach the copied output");
    require(async.image.rgba == single.image.rgba, "Async compute changed the image");

    const auto chainSingle = graphicsComputeGraphics(*device, kernels, false);
    const auto chainAsync  = graphicsComputeGraphics(*device, kernels, true);
    require(chainSingle.plan.batches.size() == 1, "A forced single-queue plan was split");
    require(chainSingle.image.rgba[0] == 0.25f && chainSingle.image.rgba[1] == 0.5f &&
                chainSingle.image.rgba[2] == 0.875f,
            "Inverted image is wrong");
    require(chainAsync.image.rgba == chainSingle.image.rgba, "Async compute changed the chained image");

    if (dualQueue)
    {
        const auto& first = async.plan.batches;
        require(first.size() == 2 && first[0].queue == PassQueue::eAsyncCompute && first[0].waits.empty() &&
                    first[1].queue == PassQueue::eGraphics && first[1].waits == std::vector<size_t> {0},
                "Compute-then-graphics plan must be a compute batch awaited by a graphics batch");
        const auto& chain = chainAsync.plan.batches;
        require(chain.size() == 3 && chain[0].queue == PassQueue::eGraphics &&
                    chain[1].queue == PassQueue::eAsyncCompute && chain[1].waits == std::vector<size_t> {0} &&
                    chain[2].queue == PassQueue::eGraphics && chain[2].waits == std::vector<size_t> {1},
                "Graphics-compute-graphics plan must be three batches with waits between them");
        require(chainAsync.dot.find("async compute queue") != std::string::npos &&
                    chainAsync.dot.find("label=\"wait\"") != std::string::npos,
                "Graph export lost the compute batch or its wait");
    }
    require(chainSingle.dot.starts_with("digraph RenderGraph {") && chainSingle.dot.ends_with("}\n") &&
                chainSingle.dot.find("Invert") != std::string::npos &&
                chainSingle.dot.find("inverted") != std::string::npos,
            "Graph export is not a Graphviz digraph of passes and resources");
    std::ofstream(directory / "chain.dot") << (dualQueue ? chainAsync.dot : chainSingle.dot);
    std::cout << "Graph plan passed: "
              << (dualQueue ? "graphics and compute queues" : "single queue only (no compute queue)")
              << ", single-queue parity, cross-queue waits, attachment rejection and DOT export\n";
    return 0;
}
