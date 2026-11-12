#include "support/check.hpp"

#include <vultra/gpu/image_readback.hpp>
#include <vultra/graph/render_graph.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

namespace
{
    using namespace vultra;
    using support::must;
    using support::rejects;
    using support::require;

    const std::filesystem::path kDirectory = "build/.tmp/graph-subresources";

    // The push constants of examples/basics/depth_pyramid.slang; the test kernels read the same block.
    struct Parameters
    {
        uint32_t sourceSize[2] {};
        uint32_t targetSize[2] {};
        float    time   = 0;
        uint32_t levels = 0;
    };

    // Compute kernels over one layout: binding 0 a sampled texture, binding 1 a storage texture.
    class Kernels
    {
    public:
        enum Kernel
        {
            eReduce, // The example's depth pyramid reduction.
            eFill,   // Writes a hash of the texel position to an R32F level.
            eVolume, // Writes (x, y, z, time) to an RGBA32F 3D level.
            eCount
        };

        explicit Kernels(Device& device) :
            m_Device(device)
        {
            std::filesystem::create_directories(kDirectory);
            std::ofstream(kDirectory / "fill.slang") << R"(
struct Parameters { uint2 sourceSize; uint2 targetSize; float time; uint levels; };
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
[[vk::binding(1, 0)]] RWTexture2D<float> target;
[shader("compute")] [numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (all(id.xy < parameters.targetSize))
    {
        target[id.xy] = frac(sin(dot(float2(id.xy) + parameters.time, float2(12.9898, 78.233))) * 43758.5453);
    }
}
)";
            std::ofstream(kDirectory / "volume.slang") << R"(
struct Parameters { uint3 size; float time; };
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
[[vk::binding(1, 0)]] RWTexture3D<float4> target;
[shader("compute")] [numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (all(id < parameters.size))
    {
        target[id] = float4(id, parameters.time);
    }
}
)";
            const VriDescriptorRangeDesc ranges[2] {{0, 1, VriDescriptorType_Texture, VriShaderStage_Compute},
                                                    {1, 1, VriDescriptorType_StorageTexture, VriShaderStage_Compute}};
            const VriDescriptorSetDesc   set {0, ranges, 2};
            const VriPushConstantDesc    constants {0, sizeof(Parameters), VriShaderStage_Compute};
            VriPipelineLayoutDesc        layout {};
            layout.descriptorSets   = &set;
            layout.descriptorSetNum = 1;
            layout.pushConstants    = &constants;
            layout.pushConstantNum  = 1;
            layout.shaderStages     = VriShaderStage_Compute;
            must(check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout), "Subresource layout"));
            VriDescriptorPoolDesc pool {};
            pool.descriptorSetMaxNum  = uint32_t(m_Sets.size());
            pool.textureMaxNum        = uint32_t(m_Sets.size());
            pool.storageTextureMaxNum = uint32_t(m_Sets.size());
            must(check(device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool), "Subresource pool"));
            must(check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, m_Sets.data(), uint32_t(m_Sets.size())),
                       "Subresource sets"));
            const std::array<std::pair<std::filesystem::path, const char*>, eCount> entries {
                std::pair {std::filesystem::path("examples/basics/depth_pyramid.slang"), "reduce"},
                std::pair {kDirectory / "fill.slang", "main"},
                std::pair {kDirectory / "volume.slang", "main"}};
            for (size_t index = 0; index < entries.size(); ++index)
            {
                m_Pipelines[index] = must(ShaderPipeline::create(
                    device,
                    entries[index].first,
                    std::vector<ShaderEntry> {{entries[index].second, VriShaderStage_Compute}},
                    [this](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, Error>
                    {
                        VriPipeline*                 result = nullptr;
                        const VriComputePipelineDesc desc {m_Layout, shaders[0], m_Device.pipelineCache};
                        if (auto created = check(m_Device.core.CreateComputePipeline(m_Device.handle, &desc, &result),
                                                 "Subresource pipeline");
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

        Kernels(const Kernels&)            = delete;
        Kernels& operator=(const Kernels&) = delete;

        // A recorded set must not change before its submission completes, so every dispatch of a frame has its
        // own; call between frames.
        void beginFrame()
        {
            m_Next = 0;
        }

        // A null source leaves binding 0 unwritten; the fill kernels do not declare it.
        std::expected<void, Error> dispatch(VriCommandBuffer*       cmd,
                                            Kernel                  kernel,
                                            VriDescriptor*          source,
                                            VriDescriptor*          target,
                                            const Parameters&       parameters,
                                            std::array<uint32_t, 3> groups)
        {
            if (m_Next == m_Sets.size())
            {
                return makeError("Subresource test ran out of descriptor sets");
            }
            auto*                              set = m_Sets[m_Next++];
            const VriDescriptor*               views[2] {source, target};
            const VriDescriptorRangeUpdateDesc updates[2] {{&views[0], 1}, {&views[1], 1}};
            if (source)
            {
                m_Device.core.UpdateDescriptorRanges(set, 0, 2, updates);
            }
            else
            {
                m_Device.core.UpdateDescriptorRanges(set, 1, 1, &updates[1]);
            }
            m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
            m_Device.core.CmdSetPipeline(cmd, m_Pipelines[kernel]->handle());
            m_Device.core.CmdSetDescriptorSet(cmd, 0, set);
            m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
            const VriDispatchDesc dispatch {groups[0], groups[1], groups[2]};
            m_Device.core.CmdDispatch(cmd, &dispatch);
            return {};
        }

    private:
        Device&                                             m_Device;
        VriPipelineLayout*                                  m_Layout = nullptr;
        VriDescriptorPool*                                  m_Pool   = nullptr;
        std::array<VriDescriptorSet*, 32>                   m_Sets {};
        std::array<std::unique_ptr<ShaderPipeline>, eCount> m_Pipelines;
        size_t                                              m_Next = 0;
    };

    Extent levelSize(Extent size, uint32_t mip)
    {
        return {std::max(size.width >> mip, 1u), std::max(size.height >> mip, 1u)};
    }

    uint32_t groups(uint32_t texels, uint32_t group)
    {
        return (texels + group - 1) / group;
    }

    void run(Frame& frame, RenderGraph& graph, Kernels* kernels = nullptr)
    {
        if (kernels)
        {
            kernels->beginFrame();
        }
        must(frame.begin());
        must(graph.execute(frame));
        must(frame.submitAndWait());
    }

    std::expected<void, Error>
    clearView(Device& device, VriCommandBuffer* cmd, VriDescriptor* view, Extent size, std::array<float, 4> color)
    {
        beginColorPass(device, cmd, view, size, color.data());
        device.core.CmdEndRendering(cmd);
        return {};
    }

    // Copies every mip of `from` to `to`, all layers or all depth slices at once.
    void copyMips(Device& device, VriCommandBuffer* cmd, Texture& from, Texture& to)
    {
        for (uint32_t mip = 0; mip < from.desc.mipNum; ++mip)
        {
            VriTextureCopyDesc copy {};
            copy.src.mip      = mip;
            copy.src.layerNum = from.desc.layerNum;
            copy.src.aspect   = VriImageAspect_Color;
            copy.dst          = copy.src;
            device.core.CmdCopyTexture(cmd, to.handle, from.handle, &copy);
        }
    }

    float red(const Image& image, uint32_t x, uint32_t y)
    {
        return image.rgba[(size_t(y) * image.size.width + x) * 4];
    }

    // The reduction the example's shader documents: a 2x2 maximum whose last column and row also take the
    // texels an odd level leaves over.
    std::vector<float> reduceOnCpu(const Image& source, Extent target)
    {
        std::vector<float> result(size_t(target.width) * target.height);
        for (uint32_t y = 0; y < target.height; ++y)
        {
            for (uint32_t x = 0; x < target.width; ++x)
            {
                const uint32_t lastX =
                    std::min(x + 1 == target.width ? source.size.width - 1 : x * 2 + 1, source.size.width - 1);
                const uint32_t lastY =
                    std::min(y + 1 == target.height ? source.size.height - 1 : y * 2 + 1, source.size.height - 1);
                float value = 0;
                for (uint32_t sy = y * 2; sy <= lastY; ++sy)
                {
                    for (uint32_t sx = x * 2; sx <= lastX; ++sx)
                    {
                        value = std::max(value, red(source, sx, sy));
                    }
                }
                result[size_t(y) * target.width + x] = value;
            }
        }
        return result;
    }

    struct Pyramid
    {
        std::vector<Image>    levels;
        RenderGraph::Snapshot plan;
        std::string           dot;
    };

    // Level 0 is filled by a compute pass on the graphics queue; each further level samples the level above
    // and writes its own mip in one pass, on the async compute queue when `async` is set.
    Pyramid depthPyramid(Device& device, Kernels& kernels, Extent size, bool async)
    {
        const uint32_t levels = std::bit_width(std::max(size.width, size.height));
        auto           desc   = colorTexture(size, VriFormat_R32_SFLOAT);
        desc.mipNum           = levels;
        desc.usage =
            VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ShaderResource | VriTextureUsage_TransferSrc;
        RenderGraph graph(device);
        const auto  hzb = must(graph.createTexture("depth.hzb", desc));
        must(graph.addPass("HZB fill",
                           {{hzb, Usage::eStorageWrite, true, {.mip = 0}}},
                           [&kernels, hzb, size](VriCommandBuffer* cmd, RenderGraph& resources)
                           {
                               auto&      texture = resources.getTexture(hzb);
                               const auto target  = texture.view({.mip = 0});
                               if (!target)
                               {
                                   return std::expected<void, Error>(std::unexpected(target.error()));
                               }
                               Parameters parameters {{}, {size.width, size.height}, 0.5f, 0};
                               return kernels.dispatch(cmd,
                                                       Kernels::eFill,
                                                       nullptr,
                                                       *target,
                                                       parameters,
                                                       {groups(size.width, 8), groups(size.height, 8), 1});
                           }));
        for (uint32_t mip = 1; mip < levels; ++mip)
        {
            must(graph.addPass(
                "HZB " + std::to_string(mip),
                {{hzb, Usage::eSampled, false, {.mip = mip - 1}}, {hzb, Usage::eStorageWrite, true, {.mip = mip}}},
                [&kernels, hzb, size, mip](VriCommandBuffer* cmd, RenderGraph& resources) -> std::expected<void, Error>
                {
                    auto&      texture = resources.getTexture(hzb);
                    const auto source  = texture.view({.mip = mip - 1});
                    const auto target  = texture.view({.mip = mip});
                    if (!source || !target)
                    {
                        return std::unexpected(source ? target.error() : source.error());
                    }
                    const auto above = levelSize(size, mip - 1);
                    const auto below = levelSize(size, mip);
                    Parameters parameters {{above.width, above.height}, {below.width, below.height}, 0, 0};
                    return kernels.dispatch(cmd,
                                            Kernels::eReduce,
                                            *source,
                                            *target,
                                            parameters,
                                            {groups(below.width, 8), groups(below.height, 8), 1});
                },
                {.queue = async ? PassQueue::eAsyncCompute : PassQueue::eGraphics}));
        }
        // A capture copies the subresources its pass wrote, here level 2 only.
        const auto captured = must(graph.captureAfterPass("HZB 2", hzb, "depth.hzb.after2"));
        must(graph.exportResource(hzb));
        must(graph.compile(false, async));
        require(graph.activePasses().size() == levels + 1, "A pyramid level or the capture was culled");
        const auto frame = must(Frame::create(device));
        // Two frames: the second starts from the per-level states the first one left behind.
        for (int iteration = 0; iteration < 2; ++iteration)
        {
            run(*frame, graph, &kernels);
        }
        Pyramid result;
        for (uint32_t mip = 0; mip < levels; ++mip)
        {
            result.levels.push_back(must(readback(device, graph.getTexture(hzb), mip)));
        }
        require(must(readback(device, graph.getTexture(captured), 2)).rgba == result.levels[2].rgba,
                "The capture after level 2 does not hold level 2");
        rejects(readback(device, graph.getTexture(captured), 1),
                "A capture read back a level its pass did not write",
                "initialized");
        result.plan = must(graph.snapshot());
        std::ostringstream dot;
        must(graph.writeDot(dot));
        result.dot = dot.str();
        return result;
    }

    void checkPyramid(const Pyramid& pyramid, Extent size)
    {
        require(pyramid.levels.size() == size_t(std::bit_width(std::max(size.width, size.height))),
                "The pyramid has the wrong number of levels");
        bool varied = false;
        for (size_t mip = 1; mip < pyramid.levels.size(); ++mip)
        {
            const auto& level    = pyramid.levels[mip];
            const auto  expected = reduceOnCpu(pyramid.levels[mip - 1], levelSize(size, uint32_t(mip)));
            require(level.size == levelSize(size, uint32_t(mip)), "A pyramid level has the wrong size");
            for (uint32_t y = 0; y < level.size.height; ++y)
            {
                for (uint32_t x = 0; x < level.size.width; ++x)
                {
                    require(red(level, x, y) == expected[size_t(y) * level.size.width + x],
                            "A pyramid level is not the 2x2 maximum of the level above (level " + std::to_string(mip) +
                                ", texel " + std::to_string(x) + ", " + std::to_string(y) + ")");
                }
            }
            varied = varied || red(level, 0, 0) != red(pyramid.levels[0], 0, 0);
        }
        require(varied, "The filled level 0 is constant; the reduction was not exercised");
    }

    void pyramids(Device& device, Kernels& kernels)
    {
        for (const Extent size : {Extent {64, 64}, Extent {37, 21}})
        {
            const auto single = depthPyramid(device, kernels, size, false);
            checkPyramid(single, size);
            const auto async = depthPyramid(device, kernels, size, true);
            for (size_t mip = 0; mip < single.levels.size(); ++mip)
            {
                require(async.levels[mip].rgba == single.levels[mip].rgba, "Async compute changed a pyramid level");
            }
            const auto& uses = single.plan.passes[1].uses;
            require(uses.size() == 2 && uses[0].range.mip == 0u && uses[1].range.mip == 1u && !uses[0].range.layer &&
                        single.plan.passes[1].dependencies == std::vector<size_t> {0},
                    "The plan lost the levels a pass reads and writes");
            require(single.dot.find("mip 0") != std::string::npos && single.dot.find("mip 1") != std::string::npos,
                    "Graph export does not name the levels a pass uses");
            std::ofstream(kDirectory / ("pyramid-" + std::to_string(size.width) + ".dot")) << single.dot;
            if (device.computeQueue)
            {
                require(async.plan.batches.size() == 3, "Async reduction should run between two graphics batches");
            }
        }
    }

    VriTextureDesc layeredDesc(VriTextureType type, uint32_t layers, uint32_t mips)
    {
        auto desc     = colorTexture({4, 4}, VriFormat_RGBA32_SFLOAT);
        desc.type     = type;
        desc.layerNum = layers;
        desc.mipNum   = mips;
        return desc;
    }

    // Each layer is written by its own pass; a write to layer 1 that a later pass replaces is culled, the
    // others are not, and no layer's content reaches another.
    void arrayLayers(Device& device)
    {
        RenderGraph graph(device);
        const auto  array = must(graph.createTexture("layers", layeredDesc(VriTextureType_2DArray, 4, 1)));
        const auto  layer = [&](std::string name, uint32_t index, std::array<float, 4> color)
        {
            must(graph.addPass(std::move(name),
                               {{array, Usage::eColorWrite, true, {.layer = index}}},
                               [&device, array, index, color](VriCommandBuffer* cmd, RenderGraph& resources)
                               {
                                   const auto view = resources.getTexture(array).view({.layer = index});
                                   if (!view)
                                   {
                                       return std::expected<void, Error>(std::unexpected(view.error()));
                                   }
                                   return clearView(device, cmd, *view, {4, 4}, color);
                               }));
        };
        layer("Layer 0", 0, {0, 0, 0, 1});
        layer("Layer 1 replaced", 1, {9, 9, 9, 1});
        layer("Layer 2", 2, {2, 0, 0, 1});
        layer("Layer 1", 1, {1, 0, 0, 1});
        layer("Layer 3", 3, {3, 0, 0, 1});
        must(graph.exportResource(array));
        must(graph.compile());
        require(graph.activePasses() == std::vector<std::string> {"Layer 0", "Layer 2", "Layer 1", "Layer 3"},
                "Per-layer liveness kept a replaced write or culled a write to another layer");
        const auto frame = must(Frame::create(device));
        run(*frame, graph);
        for (uint32_t index = 0; index < 4; ++index)
        {
            const auto image = must(readback(device, graph.getTexture(array), 0, index));
            for (size_t texel = 0; texel < image.rgba.size(); texel += 4)
            {
                require(image.rgba[texel] == float(index) && image.rgba[texel + 1] == 0 && image.rgba[texel + 2] == 0 &&
                            image.rgba[texel + 3] == 1,
                        "Array layer " + std::to_string(index) + " does not hold what its pass wrote");
            }
        }
    }

    // Every face and mip of a cube is written separately; the whole cube is then copied, which starts from
    // twelve subresources in one state each.
    void cubeFaces(Device& device)
    {
        RenderGraph graph(device);
        const auto  cube = must(graph.createTexture("cube", layeredDesc(VriTextureType_Cube, 6, 2)));
        const auto  copy = must(graph.createTexture("cube.copy", layeredDesc(VriTextureType_Cube, 6, 2)));
        for (uint32_t mip = 0; mip < 2; ++mip)
        {
            for (uint32_t face = 0; face < 6; ++face)
            {
                must(graph.addPass("Face " + std::to_string(face) + " mip " + std::to_string(mip),
                                   {{cube, Usage::eColorWrite, true, {.mip = mip, .layer = face}}},
                                   [&device, cube, mip, face](VriCommandBuffer* cmd, RenderGraph& resources)
                                   {
                                       const auto view = resources.getTexture(cube).view({.mip = mip, .layer = face});
                                       if (!view)
                                       {
                                           return std::expected<void, Error>(std::unexpected(view.error()));
                                       }
                                       return clearView(device,
                                                        cmd,
                                                        *view,
                                                        levelSize({4, 4}, mip),
                                                        {float(face), float(mip), 0.5f, 1});
                                   }));
            }
        }
        must(graph.addPass("Copy cube",
                           {{cube, Usage::eCopySource}, {copy, Usage::eCopyDestination, true}},
                           [&device, cube, copy](VriCommandBuffer* cmd, RenderGraph& resources)
                           {
                               copyMips(device, cmd, resources.getTexture(cube), resources.getTexture(copy));
                               return std::expected<void, Error>();
                           }));
        must(graph.exportResource(copy));
        must(graph.exportResource(cube));
        must(graph.compile());
        require(graph.activePasses().size() == 13, "A cube face pass was culled");
        const auto frame = must(Frame::create(device));
        run(*frame, graph);
        run(*frame, graph);
        for (const auto resource : {cube, copy})
        {
            for (uint32_t mip = 0; mip < 2; ++mip)
            {
                for (uint32_t face = 0; face < 6; ++face)
                {
                    const auto image = must(readback(device, graph.getTexture(resource), mip, face));
                    require(image.size == levelSize({4, 4}, mip), "A cube level read back with the wrong size");
                    for (size_t texel = 0; texel < image.rgba.size(); texel += 4)
                    {
                        require(image.rgba[texel] == float(face) && image.rgba[texel + 1] == float(mip) &&
                                    image.rgba[texel + 2] == 0.5f && image.rgba[texel + 3] == 1,
                                "Cube face " + std::to_string(face) + " mip " + std::to_string(mip) +
                                    " does not read back what was written");
                    }
                }
            }
        }
    }

    // A 3D texture has no layers: passes name its mips, and readback reads one depth slice.
    void volumeSlices(Device& device, Kernels& kernels)
    {
        auto desc   = colorTexture({8, 8}, VriFormat_RGBA32_SFLOAT);
        desc.type   = VriTextureType_3D;
        desc.depth  = 4;
        desc.mipNum = 2;
        desc.usage  = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferSrc;
        RenderGraph graph(device);
        const auto  volume = must(graph.createTexture("volume", desc));
        for (uint32_t mip = 0; mip < 2; ++mip)
        {
            must(graph.addPass(
                "Volume mip " + std::to_string(mip),
                {{volume, Usage::eStorageWrite, true, {.mip = mip}}},
                [&kernels, volume, mip](VriCommandBuffer* cmd, RenderGraph& resources) -> std::expected<void, Error>
                {
                    const auto target = resources.getTexture(volume).view({.mip = mip});
                    if (!target)
                    {
                        return std::unexpected(target.error());
                    }
                    const uint32_t extent[3] {8u >> mip, 8u >> mip, 4u >> mip};
                    Parameters     parameters {};
                    // The volume kernel reads uint3 size and float time from the same bytes.
                    std::memcpy(&parameters, extent, sizeof(extent));
                    const float tag = float(mip);
                    std::memcpy(reinterpret_cast<std::byte*>(&parameters) + sizeof(extent), &tag, sizeof(tag));
                    return kernels.dispatch(cmd,
                                            Kernels::eVolume,
                                            nullptr,
                                            *target,
                                            parameters,
                                            {groups(extent[0], 4), groups(extent[1], 4), groups(extent[2], 4)});
                }));
        }
        must(graph.exportResource(volume));
        must(graph.compile());
        const auto frame = must(Frame::create(device));
        run(*frame, graph, &kernels);
        for (uint32_t mip = 0; mip < 2; ++mip)
        {
            for (uint32_t slice = 0; slice < (4u >> mip); ++slice)
            {
                const auto image = must(readback(device, graph.getTexture(volume), mip, slice));
                for (uint32_t y = 0; y < image.size.height; ++y)
                {
                    for (uint32_t x = 0; x < image.size.width; ++x)
                    {
                        const auto pixel = must(imagePixel(image, x, y));
                        require(pixel[0] == float(x) && pixel[1] == float(y) && pixel[2] == float(slice) &&
                                    pixel[3] == float(mip),
                                "Volume mip " + std::to_string(mip) + " slice " + std::to_string(slice) +
                                    " does not read back what was written");
                    }
                }
            }
        }
        rejects(readback(device, graph.getTexture(volume), 1, 2), "Readback accepted a slice beyond the mip's depth");
    }

    // A history texture with mips: every level starts at the initial value, each frame writes its levels in
    // separate passes, and the next frame reads all of them.
    void historyMips(Device& device)
    {
        auto desc   = colorTexture({4, 4}, VriFormat_RGBA32_SFLOAT);
        desc.mipNum = 3;
        desc.usage  = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc;
        auto observedDesc  = desc;
        observedDesc.usage = VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc;
        RenderGraph         graph(device);
        const VriClearColor initial {{0.25f, 0.5f, 1, 1}};
        const auto          retained = must(graph.createHistoryTexture("retained", desc, initial));
        const auto          observed = must(graph.createTexture("previous", observedDesc));
        // One host buffer holds a full 4x4 RGBA32F level per mip; each frame fills it before recording.
        constexpr uint64_t kLevelBytes = 4 * 4 * 16;
        auto               staging     = must(
            Buffer::create(device, {3 * kLevelBytes, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload}));
        must(graph.addPass("Read previous frame",
                           {{retained, Usage::eCopySource}, {observed, Usage::eCopyDestination, true}},
                           [&device, retained, observed](VriCommandBuffer* cmd, RenderGraph& resources)
                           {
                               copyMips(device, cmd, resources.getTexture(retained), resources.getTexture(observed));
                               return std::expected<void, Error>();
                           }));
        for (uint32_t mip = 0; mip < 3; ++mip)
        {
            must(graph.addPass("Store mip " + std::to_string(mip),
                               {{retained, Usage::eCopyDestination, true, {.mip = mip}}},
                               [&device, &staging, retained, mip](VriCommandBuffer* cmd, RenderGraph& resources)
                               {
                                   VriBufferTextureCopyDesc region {};
                                   region.bufferOffset     = mip * kLevelBytes;
                                   region.bufferRowLength  = 4;
                                   region.texture.mip      = mip;
                                   region.texture.layerNum = 1;
                                   region.texture.aspect   = VriImageAspect_Color;
                                   region.texture.width    = 4u >> mip;
                                   region.texture.height   = 4u >> mip;
                                   region.texture.depth    = 1;
                                   device.core.CmdUploadBufferToTexture(cmd,
                                                                        resources.getTexture(retained).handle,
                                                                        staging.handle,
                                                                        &region);
                                   return std::expected<void, Error>();
                               }));
        }
        must(graph.exportResource(observed));
        must(graph.compile());
        require(graph.activePasses().size() == 4, "A history level writer was culled");
        const auto handle = graph.getTexture(retained).handle;
        const auto frame  = must(Frame::create(device));
        const auto check  = [&](const std::array<float, 4>& expected, uint32_t mip, const std::string& message)
        {
            const auto image = must(readback(device, graph.getTexture(observed), mip));
            for (size_t texel = 0; texel < image.rgba.size(); texel += 4)
            {
                require(std::equal(expected.begin(), expected.end(), image.rgba.begin() + texel), message);
            }
        };
        const auto runFrame = [&](float value)
        {
            auto* data = static_cast<float*>(device.core.MapBuffer(staging.handle, 0, 3 * kLevelBytes));
            require(data != nullptr, "Map history staging buffer");
            for (uint32_t mip = 0; mip < 3; ++mip)
            {
                std::fill_n(data + mip * kLevelBytes / 4, kLevelBytes / 4, value + float(mip) * 10);
            }
            device.core.UnmapBuffer(staging.handle);
            run(*frame, graph);
        };
        runFrame(1);
        for (uint32_t mip = 0; mip < 3; ++mip)
        {
            check({0.25f, 0.5f, 1, 1},
                  mip,
                  "History level " + std::to_string(mip) + " did not start at its initial value");
        }
        runFrame(2);
        for (uint32_t mip = 0; mip < 3; ++mip)
        {
            const float value = 1 + float(mip) * 10;
            check({value, value, value, value},
                  mip,
                  "History level " + std::to_string(mip) + " did not survive a frame");
        }
        graph.resetHistory();
        runFrame(3);
        for (uint32_t mip = 0; mip < 3; ++mip)
        {
            check({0.25f, 0.5f, 1, 1}, mip, "History reset did not restore level " + std::to_string(mip));
        }
        require(graph.getTexture(retained).handle == handle, "History reset recreated the texture");
    }

    RenderGraph::ExecutePass nothing()
    {
        return [](VriCommandBuffer*, RenderGraph&) -> std::expected<void, Error>
        {
            return {};
        };
    }

    void rejected(Device& device)
    {
        auto desc        = colorTexture({8, 8}, VriFormat_R32_SFLOAT);
        desc.mipNum      = 4;
        desc.usage       = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ShaderResource;
        auto volumeDesc  = desc;
        volumeDesc.type  = VriTextureType_3D;
        volumeDesc.depth = 4;

        // Each case adds one pass after a writer of every subresource and expects compile() to fail.
        struct Case
        {
            const char*                                   what;
            const char*                                   cause;
            TextureRange                                  read;
            std::optional<std::pair<Usage, TextureRange>> second;
            bool                                          volume = false;
        };

        const std::array cases {
            Case {"A mip beyond the chain compiled", "out of range", {.mip = 4}, {}},
            Case {"A layer beyond a single-layer texture compiled", "out of range", {.layer = 1}, {}},
            Case {"A layer of a 3D texture compiled", "3D", {.layer = 0}, {}, true},
            Case {"Reading and writing one mip in one pass compiled",
                  "same subresource",
                  {.mip = 1},
                  std::pair {Usage::eStorageWrite, TextureRange {.mip = 1}}},
            Case {"Reading the whole texture while writing one mip compiled",
                  "same subresource",
                  {},
                  std::pair {Usage::eStorageWrite, TextureRange {.mip = 2}}},
        };
        for (const auto& item : cases)
        {
            RenderGraph graph(device);
            const auto  texture = must(graph.createTexture("texture", item.volume ? volumeDesc : desc));
            must(graph.addPass("Writer", {{texture, Usage::eStorageWrite, true}}, nothing()));
            std::vector<RenderGraph::Use> uses {{texture, Usage::eSampled, false, item.read}};
            if (item.second)
            {
                uses.push_back({texture, item.second->first, false, item.second->second});
            }
            must(graph.addPass("Invalid", std::span<const RenderGraph::Use>(uses), nothing(), true));
            rejects(graph.compile(), item.what, item.cause);
        }
        {
            RenderGraph graph(device);
            const auto  texture = must(graph.createTexture("texture", desc));
            must(graph.addPass("Level 0", {{texture, Usage::eStorageWrite, true, {.mip = 0}}}, nothing()));
            must(graph.addPass("Level 1 reader", {{texture, Usage::eSampled, false, {.mip = 1}}}, nothing(), true));
            rejects(graph.compile(), "A level was read before any pass wrote it", "read before first write");
        }
        {
            RenderGraph graph(device);
            const auto  buffer =
                must(graph.createBuffer("buffer", {64, 0, VriBufferUsage_StorageBuffer, VriMemoryLocation_Device}));
            must(graph.addPass("Buffer mip", {{buffer, Usage::eStorageWrite, true, {.mip = 0}}}, nothing(), true));
            rejects(graph.compile(), "A buffer use named a subresource", "subresource");
        }
        RenderGraph graph(device);
        auto        cube = desc;
        cube.type        = VriTextureType_Cube;
        cube.layerNum    = 4;
        rejects(graph.createTexture("cube", cube), "A cube with four layers was accepted", "multiple of 6");
        auto layered     = desc;
        layered.layerNum = 2;
        rejects(graph.createTexture("layered", layered), "A 2D texture with two layers was accepted", "2DArray");
        auto deep   = desc;
        deep.mipNum = 5;
        rejects(graph.createTexture("deep", deep), "A mip chain longer than the texture was accepted", "mip");
    }
} // namespace

int main()
{
    const auto device = must(Device::create());
    Kernels    kernels(*device);
    rejected(*device);
    pyramids(*device, kernels);
    arrayLayers(*device);
    cubeFaces(*device);
    volumeSlices(*device, kernels);
    historyMips(*device);
    std::cout << "Graph subresources passed: depth pyramids (" << (device->computeQueue ? "with" : "without")
              << " a compute queue), array layers, cube faces, volume slices, history mips and rejected uses\n";
    return 0;
}
