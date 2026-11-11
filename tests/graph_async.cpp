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
#include <stdexcept>

namespace
{
    using namespace vultra;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

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
            check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout), "Async test layout");
            VriDescriptorPoolDesc pool {};
            pool.descriptorSetMaxNum  = uint32_t(m_Sets.size());
            pool.textureMaxNum        = uint32_t(m_Sets.size());
            pool.storageTextureMaxNum = uint32_t(m_Sets.size());
            check(device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool), "Async test pool");
            check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, m_Sets.data(), uint32_t(m_Sets.size())),
                  "Async test sets");
            const std::array names {"fill", "invert"};
            for (size_t index = 0; index < names.size(); ++index)
            {
                m_Pipelines[index] = std::make_unique<ShaderPipeline>(
                    device,
                    file,
                    std::vector<ShaderEntry> {{names[index], VriShaderStage_Compute}},
                    [this](std::span<const VriShaderDesc> shaders)
                    {
                        VriPipeline*                 result = nullptr;
                        const VriComputePipelineDesc desc {m_Layout, shaders[0], m_Device.pipelineCache};
                        check(m_Device.core.CreateComputePipeline(m_Device.handle, &desc, &result),
                              "Async test pipeline");
                        return result;
                    });
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
        void dispatch(VriCommandBuffer* cmd, size_t kernel, Texture* input, Texture& output)
        {
            const VriDescriptor*               views[2] {input ? input->view() : m_Placeholder->view(), output.view()};
            const VriDescriptorRangeUpdateDesc updates[2] {{&views[0], 1}, {&views[1], 1}};
            m_Device.core.UpdateDescriptorRanges(m_Sets[kernel], 0, 2, updates);
            m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
            m_Device.core.CmdSetPipeline(cmd, m_Pipelines[kernel]->handle());
            m_Device.core.CmdSetDescriptorSet(cmd, 0, m_Sets[kernel]);
            const VriDispatchDesc groups {kSize.width / 8, kSize.height / 8, 1};
            m_Device.core.CmdDispatch(cmd, &groups);
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
        graph.addPass("Copy to output",
                      {{from, Usage::eCopySource}, {to, Usage::eCopyDestination, true}},
                      [from, to](VriCommandBuffer* cmd, RenderGraph& resources)
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
                      });
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
            graph.createTexture("filled",
                                imageDesc(VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ShaderResource |
                                          VriTextureUsage_TransferSrc));
        const auto output =
            graph.createTexture("output", imageDesc(VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc));
        graph.addPass("Fill",
                      {{filled, Usage::eStorageWrite, true}},
                      [&kernels, filled](VriCommandBuffer* cmd, RenderGraph& resources)
                      {
                          kernels.dispatch(cmd, 0, nullptr, resources.getTexture(filled));
                      },
                      {.queue = PassQueue::eAsyncCompute});
        copyPass(graph, filled, output);
        graph.exportResource(output);
        graph.compile(false, async);
        kernels.setPlaceholder(placeholder);
        Frame frame(device);
        // Two frames: the second starts from the states the first one left behind.
        for (int iteration = 0; iteration < 2; ++iteration)
        {
            frame.begin();
            graph.execute(frame);
            frame.submitAndWait();
        }
        std::ostringstream dot;
        graph.writeDot(dot);
        return {readback(device, graph.getTexture(output)), graph.snapshot(), dot.str()};
    }

    // A graphics pass renders an image, async compute inverts it, and a graphics pass copies the result:
    // the compute batch sits between two graphics submissions.
    Result graphicsComputeGraphics(Device& device, Kernels& kernels, bool async)
    {
        RenderGraph graph(device);
        const auto  cleared = graph.createTexture("cleared", colorTexture(kSize, VriFormat_RGBA16_SFLOAT));
        const auto  inverted =
            graph.createTexture("inverted",
                                imageDesc(VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ShaderResource |
                                          VriTextureUsage_TransferSrc));
        const auto output =
            graph.createTexture("output", imageDesc(VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc));
        graph.addPass("Clear",
                      {{cleared, Usage::eColorWrite, true}},
                      [cleared](VriCommandBuffer* cmd, RenderGraph& resources)
                      {
                          const float color[4] {0.75f, 0.5f, 0.125f, 1};
                          beginColorPass(resources.device(), cmd, resources.getTexture(cleared).view(), kSize, color);
                          resources.device().core.CmdEndRendering(cmd);
                      });
        graph.addPass("Invert",
                      {{cleared, Usage::eSampled}, {inverted, Usage::eStorageWrite, true}},
                      [&kernels, cleared, inverted](VriCommandBuffer* cmd, RenderGraph& resources)
                      {
                          kernels.dispatch(cmd, 1, &resources.getTexture(cleared), resources.getTexture(inverted));
                      },
                      {.queue = PassQueue::eAsyncCompute});
        copyPass(graph, inverted, output);
        graph.exportResource(output);
        graph.compile(false, async);
        Frame frame(device);
        for (int iteration = 0; iteration < 2; ++iteration)
        {
            frame.begin();
            graph.execute(frame);
            frame.submitAndWait();
        }
        if (graph.snapshot().batches.size() > 1)
        {
            bool rejected = false;
            try
            {
                graph.execute(frame.begin());
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
            frame.submitAndWait();
            require(rejected, "A multi-queue plan was recorded into a single command buffer");
        }
        std::ostringstream dot;
        graph.writeDot(dot);
        return {readback(device, graph.getTexture(output)), graph.snapshot(), dot.str()};
    }

    void attachmentRejected(Device& device)
    {
        RenderGraph graph(device);
        const auto  target = graph.createTexture("target", colorTexture(kSize));
        graph.addPass("Compute attachment",
                      {{target, Usage::eColorWrite, true}},
                      [](VriCommandBuffer*, RenderGraph&)
                      {
                      },
                      {.queue = PassQueue::eAsyncCompute});
        graph.exportResource(target);
        bool rejected = false;
        try
        {
            graph.compile();
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "An async compute pass with an attachment usage compiled");
    }
} // namespace

int main()
try
{
    const auto directory = std::filesystem::path("build/.tmp/graph-async") /
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(directory);
    Device  device;
    Kernels kernels(device, directory);
    Texture placeholder(device, imageDesc(VriTextureUsage_ShaderResource | VriTextureUsage_ConcurrentQueues));
    {
        // The placeholder is sampled by nothing, but its view must be in a readable layout.
        Frame frame(device);
        placeholder.transition(frame.begin(),
                               {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands});
        frame.submitAndWait();
    }
    attachmentRejected(device);

    const bool dualQueue = device.computeQueue != nullptr;
    const auto single    = computeThenGraphics(device, kernels, placeholder, false);
    const auto async     = computeThenGraphics(device, kernels, placeholder, true);
    require(single.plan.batches.size() == 1 && single.plan.batches[0].queue == PassQueue::eGraphics,
            "A plan without async compute must be one graphics batch");
    // RGBA16F cannot represent these fractions exactly.
    require(std::abs(single.image.rgba[(5 * 16 + 9) * 4] - 9 / 15.0f) < 1e-3f &&
                std::abs(single.image.rgba[(5 * 16 + 9) * 4 + 1] - 5 / 15.0f) < 1e-3f,
            "Compute fill did not reach the copied output");
    require(async.image.rgba == single.image.rgba, "Async compute changed the image");

    const auto chainSingle = graphicsComputeGraphics(device, kernels, false);
    const auto chainAsync  = graphicsComputeGraphics(device, kernels, true);
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
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
