#include "support/check.hpp"

#include <vultra/gpu/image_readback.hpp>
#include <vultra/graph/render_graph.hpp>

#include <array>
#include <iostream>

namespace
{
    using namespace vultra;
    using support::must;
    using support::require;

    void run(Frame& frame, RenderGraph& graph)
    {
        must(graph.execute(must(frame.begin())));
        must(frame.submitAndWait());
    }

    // Begins and ends an 8x8 color pass on the target; a null color loads its previous content.
    std::expected<void, Error> colorPass(Device& device, VriCommandBuffer* cmd, Texture& target, const float* color)
    {
        const auto view = target.view();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        beginColorPass(device, cmd, *view, {8, 8}, color);
        device.core.CmdEndRendering(cmd);
        return {};
    }

    void copy(Device& device, VriCommandBuffer* cmd, Texture& from, Texture& to)
    {
        VriTextureCopyDesc desc {};
        desc.src.layerNum = 1;
        desc.dst.layerNum = 1;
        desc.src.aspect   = VriImageAspect_Color;
        desc.dst.aspect   = VriImageAspect_Color;
        device.core.CmdCopyTexture(cmd, to.handle, from.handle, &desc);
    }

    void clearPass(Device&               device,
                   RenderGraph&          graph,
                   const char*           name,
                   RenderGraph::Resource target,
                   std::array<float, 4>  color)
    {
        must(graph.addPass(name,
                           {{target, Usage::eColorWrite, true}},
                           [&, target, color](VriCommandBuffer* cmd, RenderGraph& current)
                           {
                               return colorPass(device, cmd, current.getTexture(target), color.data());
                           }));
    }

    void overwrittenContent(Device& device)
    {
        RenderGraph graph(device);
        const auto  texture = must(graph.createTexture("color", colorTexture({8, 8})));
        const auto  unused  = must(graph.createTexture("unused", colorTexture({8, 8})));
        clearPass(device, graph, "Old content", texture, {1, 0, 0, 1});
        must(graph.addPass("Unused reader",
                           {{texture, Usage::eCopySource}, {unused, Usage::eCopyDestination}},
                           [&](VriCommandBuffer* cmd, RenderGraph& current) -> std::expected<void, Error>
                           {
                               copy(device, cmd, current.getTexture(texture), current.getTexture(unused));
                               return {};
                           }));
        clearPass(device, graph, "Replacement", texture, {0, 0, 1, 1});
        must(graph.exportResource(texture));
        must(graph.compile());
        require(graph.activePasses() == std::vector<std::string> {"Replacement"},
                "Ordering hazards retained overwritten content or an unused reader");
        const auto plan = must(graph.snapshot());
        require(plan.passes[2].dependencies == std::vector<size_t> {0, 1} &&
                    plan.passes[2].contentDependencies.empty() && !must(graph.resourceInfo(unused)).active,
                "Ordering and content dependencies were not distinguished");
        const auto frame = must(Frame::create(device));
        run(*frame, graph);
        const auto image = must(readback(device, graph.getTexture(texture)));
        require(image.rgba[0] == 0 && image.rgba[2] == 1, "Replacement output contains discarded data");
    }

    void preservingContent(Device& device)
    {
        RenderGraph graph(device);
        const auto  texture = must(graph.createTexture("color", colorTexture({8, 8})));
        clearPass(device, graph, "Original", texture, {1, 0, 0, 1});
        must(graph.addPass("Preserve attachment",
                           {{texture, Usage::eColorReadWrite}},
                           [&](VriCommandBuffer* cmd, RenderGraph& current)
                           {
                               return colorPass(device, cmd, current.getTexture(texture), nullptr);
                           }));
        must(graph.exportResource(texture));
        must(graph.compile());
        require(graph.activePasses() == std::vector<std::string> {"Original", "Preserve attachment"},
                "Read/write attachment lost the writer supplying its loaded content");
        const auto frame = must(Frame::create(device));
        run(*frame, graph);
        require(must(readback(device, graph.getTexture(texture))).rgba[0] == 1,
                "Attachment LOAD lost previous content");
    }

    void sideEffectReader(Device& device)
    {
        RenderGraph graph(device);
        const auto  texture  = must(graph.createTexture("color", colorTexture({8, 8})));
        const auto  observed = must(graph.createTexture("observation", colorTexture({8, 8})));
        clearPass(device, graph, "Original", texture, {1, 0, 0, 1});
        int calls = 0;
        must(graph.addPass(
            "Observation",
            {{texture, Usage::eCopySource}, {observed, Usage::eCopyDestination}},
            [&](VriCommandBuffer* cmd, RenderGraph& current) -> std::expected<void, Error>
            {
                ++calls;
                copy(device, cmd, current.getTexture(texture), current.getTexture(observed));
                return {};
            },
            true));
        clearPass(device, graph, "Replacement", texture, {0, 0, 1, 1});
        must(graph.exportResource(texture));
        must(graph.compile());
        require(graph.activePasses().size() == 3, "Side-effect reader lost its content producer");
        const auto frame = must(Frame::create(device));
        run(*frame, graph);
        require(calls == 1 && must(readback(device, graph.getTexture(observed))).rgba[0] == 1 &&
                    must(readback(device, graph.getTexture(texture))).rgba[2] == 1,
                "Write-after-read ordering changed an observed earlier value");
    }

    void overwrittenHistory(Device& device)
    {
        RenderGraph graph(device);
        auto        desc = colorTexture({8, 8}, VriFormat_RGBA32_SFLOAT);
        desc.usage |= VriTextureUsage_ShaderResourceStorage;
        const auto retained = must(graph.createHistoryTexture("history", desc));
        clearPass(device, graph, "Old history", retained, {1, 0, 0, 1});
        clearPass(device, graph, "Next frame history", retained, {0, 0, 1, 1});
        must(graph.compile(true));
        require(graph.activePasses() == std::vector<std::string> {"Next frame history"},
                "History retained a write overwritten before any observation");
        const auto frame = must(Frame::create(device));
        for (int i = 0; i < 2; ++i)
        {
            run(*frame, graph);
            const auto image = must(readback(device, graph.getTexture(retained)));
            require(image.rgba[0] == 0 && image.rgba[2] == 1, "History did not retain its final live writer");
        }
    }

    void partialCopy(Device& device)
    {
        RenderGraph graph(device);
        const auto  target = must(graph.createTexture("target", colorTexture({8, 8})));
        const auto  patch  = must(graph.createTexture("patch", colorTexture({8, 8})));
        clearPass(device, graph, "Initial content", target, {1, 0, 0, 1});
        clearPass(device, graph, "Patch data", patch, {0, 0, 1, 1});
        must(graph.addPass("Partial copy",
                           {{patch, Usage::eCopySource}, {target, Usage::eCopyDestination}},
                           [&](VriCommandBuffer* cmd, RenderGraph& current) -> std::expected<void, Error>
                           {
                               VriTextureCopyDesc region {};
                               region.src.layerNum = 1;
                               region.src.aspect   = VriImageAspect_Color;
                               region.src.width    = 4;
                               region.src.height   = 8;
                               region.src.depth    = 1;
                               region.dst          = region.src;
                               region.dst.x        = 4;
                               device.core.CmdCopyTexture(cmd,
                                                          current.getTexture(target).handle,
                                                          current.getTexture(patch).handle,
                                                          &region);
                               return {};
                           }));
        must(graph.exportResource(target));
        must(graph.compile());
        require(graph.activePasses().size() == 3, "Partial write discarded untouched destination content");
        const auto frame = must(Frame::create(device));
        run(*frame, graph);
        const auto image = must(readback(device, graph.getTexture(target)));
        for (uint32_t y = 0; y < 8; ++y)
        {
            for (uint32_t x = 0; x < 8; ++x)
            {
                const auto pixel = must(imagePixel(image, x, y));
                require(pixel[0] == (x < 4 ? 1 : 0) && pixel[2] == (x < 4 ? 0 : 1),
                        "Partial copy lost previous content or used the wrong region");
            }
        }
    }

    void invalidDiscard(Device& device)
    {
        for (const auto usage : {Usage::eColorReadWrite, Usage::eSampled, Usage::eCopySource})
        {
            RenderGraph graph(device);
            const auto  texture = must(graph.createTexture("invalid", colorTexture({8, 8})));
            must(graph.addPass("Contradictory access",
                               {{texture, usage, true}},
                               [](VriCommandBuffer*, RenderGraph&) -> std::expected<void, Error>
                               {
                                   return {};
                               }));
            support::rejects(graph.compile(),
                             "Read access accepted a declaration that discards its input content",
                             "discardPrevious");
        }
    }

    void history(Device& device)
    {
        auto desc = colorTexture({8, 8}, VriFormat_RGBA32_SFLOAT);
        desc.usage |= VriTextureUsage_ShaderResourceStorage;
        RenderGraph         graph(device);
        const VriClearColor initial {{0.25f, 0.5f, 1, 1}};
        const auto          retained = must(graph.createHistoryTexture("retained", desc, initial));
        const auto          observed = must(graph.createTexture("previous", desc));
        must(graph.addPass("Read previous frame",
                           {{retained, Usage::eCopySource}, {observed, Usage::eCopyDestination}},
                           [&](VriCommandBuffer* cmd, RenderGraph& current) -> std::expected<void, Error>
                           {
                               copy(device, cmd, current.getTexture(retained), current.getTexture(observed));
                               return {};
                           }));
        float value = 0.75f;
        // A history write remains live even when it follows the exported output's last use.
        must(graph.addPass("Store next frame",
                           {{retained, Usage::eCopyDestination}},
                           [&](VriCommandBuffer* cmd, RenderGraph& current) -> std::expected<void, Error>
                           {
                               const VriClearColor clear {{value, 0, 0, 1}};
                               device.core.CmdClearStorageTexture(cmd, current.getTexture(retained).handle, &clear);
                               value += 1;
                               return {};
                           }));
        must(graph.exportResource(observed));
        must(graph.compile(true));
        require(graph.activePasses().size() == 2, "History producer was culled");
        const auto handle = graph.getTexture(retained).handle;
        const auto epoch  = graph.historyEpoch();
        require(must(graph.resourceInfo(retained)).history && !must(graph.resourceInfo(observed)).history,
                "History resource identity is missing");
        const auto frame = must(Frame::create(device));
        for (float expected : {0.25f, 0.75f, 1.75f})
        {
            run(*frame, graph);
            require(must(readback(device, graph.getTexture(observed))).rgba[0] == expected,
                    "History was uninitialized or reset between frames");
        }
        graph.resetHistory();
        require(graph.historyEpoch() == epoch + 1, "History reset did not advance its epoch");
        run(*frame, graph);
        const auto image = must(readback(device, graph.getTexture(observed)));
        require(image.rgba[0] == 0.25f && image.rgba[1] == 0.5f && image.rgba[2] == 1,
                "History reset did not restore its initial value");
        require(graph.getTexture(retained).handle == handle, "History reset recreated GPU resources");
    }

    std::array<Image, 3> transientReuse(Device& device, bool alias, bool preserveFirst = false)
    {
        const auto  desc = colorTexture({8, 8}, VriFormat_RGBA32_SFLOAT);
        RenderGraph graph(device);
        const auto  overlapping = must(graph.createTexture("overlapping", desc));
        const auto  first       = must(graph.createTexture("first", desc));
        const auto  second      = must(graph.createTexture("second", desc));
        if (preserveFirst)
        {
            must(graph.preserveContents(first));
        }
        bool             skipFirst = false;
        const std::array outputs {must(graph.createTexture("red", desc)),
                                  must(graph.createTexture("blue", desc)),
                                  must(graph.createTexture("green", desc))};
        const auto       clear = [&](const char* name, RenderGraph::Resource resource, std::array<float, 4> color)
        {
            must(graph.addPass(
                name,
                {{resource, Usage::eColorWrite}},
                [&, resource, color](VriCommandBuffer* cmd, RenderGraph& current) -> std::expected<void, Error>
                {
                    if (skipFirst && resource.index == first.index)
                    {
                        return {};
                    }
                    return colorPass(device, cmd, current.getTexture(resource), color.data());
                }));
        };
        const auto save = [&](const char* name, RenderGraph::Resource from, RenderGraph::Resource to)
        {
            must(graph.addPass(name,
                               {{from, Usage::eCopySource}, {to, Usage::eCopyDestination}},
                               [&, from, to](VriCommandBuffer* cmd, RenderGraph& current) -> std::expected<void, Error>
                               {
                                   copy(device, cmd, current.getTexture(from), current.getTexture(to));
                                   return {};
                               }));
            must(graph.exportResource(to));
        };
        clear("Green", overlapping, {0, 1, 0, 1});
        clear("Red", first, {1, 0, 0, 1});
        save("Save red", first, outputs[0]);
        clear("Blue", second, {0, 0, 1, 1});
        save("Save blue", second, outputs[1]);
        save("Save green", overlapping, outputs[2]);
        must(graph.compile(alias));
        const auto a = must(graph.resourceInfo(first));
        const auto b = must(graph.resourceInfo(second));
        require(a.lastUse < b.firstUse, "Transient resource intervals overlap");
        require((graph.getTexture(first).handle == graph.getTexture(second).handle) == (alias && !preserveFirst),
                "Disjoint identical transients did not follow the allocation policy");
        require((a.allocation == b.allocation) == (alias && !preserveFirst), "Observer allocation identities disagree");
        require(graph.getTexture(overlapping).handle != graph.getTexture(first).handle,
                "Overlapping live resources shared storage");
        require(graph.getTexture(outputs[0]).handle != graph.getTexture(first).handle,
                "Exported output shared transient storage");
        const auto frame = must(Frame::create(device));
        run(*frame, graph);
        std::array<Image, 3> images;
        for (size_t i = 0; i < images.size(); ++i)
        {
            images[i] = must(readback(device, graph.getTexture(outputs[i])));
        }
        require(images[0].rgba[0] == 1 && images[1].rgba[2] == 1 && images[2].rgba[1] == 1,
                "Aliased storage corrupted pass outputs");
        if (preserveFirst)
        {
            skipFirst = true;
            run(*frame, graph);
            require(must(readback(device, graph.getTexture(outputs[0]))).rgba == images[0].rgba,
                    "A cached producer lost its retained contents when other transients were reused");
        }
        if (alias)
        {
            support::rejects(graph.exportResource(first), "Editing an aliased plan invalidated its proven lifetimes");
            run(*frame, graph);
            require(must(readback(device, graph.getTexture(outputs[0]))).rgba == images[0].rgba,
                    "Rejected allocation-plan edit broke the valid graph");
        }
        return images;
    }
} // namespace

int main()
{
    const auto device = must(vultra::Device::create());
    overwrittenContent(*device);
    preservingContent(*device);
    sideEffectReader(*device);
    overwrittenHistory(*device);
    partialCopy(*device);
    invalidDiscard(*device);
    history(*device);
    const auto separate = transientReuse(*device, false);
    const auto aliased  = transientReuse(*device, true);
    transientReuse(*device, true, true);
    for (size_t i = 0; i < separate.size(); ++i)
    {
        require(separate[i].rgba == aliased[i].rgba, "Aliasing changed the rendered image");
    }
    std::cout << "Graph history/reset and transient allocation parity passed\n";
    return 0;
}
