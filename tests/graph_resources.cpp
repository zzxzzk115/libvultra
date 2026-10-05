#include <vultra/servers/rendering/graph/render_graph.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <array>
#include <iostream>
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

    void copy(Device& device, VriCommandBuffer* cmd, Texture& from, Texture& to)
    {
        VriTextureCopyDesc desc {};
        desc.src.layerNum = 1;
        desc.dst.layerNum = 1;
        desc.src.aspect   = VriImageAspect_Color;
        desc.dst.aspect   = VriImageAspect_Color;
        device.core.CmdCopyTexture(cmd, to.handle, from.handle, &desc);
    }

    void history(Device& device)
    {
        auto desc = colorTexture({8, 8}, VriFormat_RGBA32_SFLOAT);
        desc.usage |= VriTextureUsage_ShaderResourceStorage;
        RenderGraph         graph(device);
        const VriClearColor initial {{0.25f, 0.5f, 1, 1}};
        const auto          retained = graph.createHistoryTexture("retained", desc, initial);
        const auto          observed = graph.createTexture("previous", desc);
        graph.addPass("Read previous frame",
                      {{retained, Usage::eCopySource}, {observed, Usage::eCopyDestination}},
                      [&](auto* cmd, auto& current)
                      {
                          copy(device, cmd, current.getTexture(retained), current.getTexture(observed));
                      });
        float value = 0.75f;
        // A history write remains live even when it follows the exported output's last use.
        graph.addPass("Store next frame",
                      {{retained, Usage::eCopyDestination}},
                      [&](auto* cmd, auto& current)
                      {
                          const VriClearColor clear {{value, 0, 0, 1}};
                          device.core.CmdClearStorageTexture(cmd, current.getTexture(retained).handle, &clear);
                          value += 1;
                      });
        graph.exportResource(observed);
        graph.compile(true);
        require(graph.activePasses().size() == 2, "History producer was culled");
        const auto handle = graph.getTexture(retained).handle;
        const auto epoch  = graph.historyEpoch();
        require(graph.resourceInfo(retained).history && !graph.resourceInfo(observed).history,
                "History resource identity is missing");
        Frame frame(device);
        for (float expected : {0.25f, 0.75f, 1.75f})
        {
            graph.execute(frame.begin());
            frame.submitAndWait();
            require(readback(device, graph.getTexture(observed)).rgba[0] == expected,
                    "History was uninitialized or reset between frames");
        }
        graph.resetHistory();
        require(graph.historyEpoch() == epoch + 1, "History reset did not advance its epoch");
        graph.execute(frame.begin());
        frame.submitAndWait();
        const auto image = readback(device, graph.getTexture(observed));
        require(image.rgba[0] == 0.25f && image.rgba[1] == 0.5f && image.rgba[2] == 1,
                "History reset did not restore its initial value");
        require(graph.getTexture(retained).handle == handle, "History reset recreated GPU resources");
    }

    std::array<Image, 3> transientReuse(Device& device, bool alias)
    {
        const auto       desc = colorTexture({8, 8}, VriFormat_RGBA32_SFLOAT);
        RenderGraph      graph(device);
        const auto       overlapping = graph.createTexture("overlapping", desc);
        const auto       first       = graph.createTexture("first", desc);
        const auto       second      = graph.createTexture("second", desc);
        const std::array outputs {graph.createTexture("red", desc),
                                  graph.createTexture("blue", desc),
                                  graph.createTexture("green", desc)};
        const auto       clear = [&](const char* name, RenderGraph::Resource resource, std::array<float, 4> color)
        {
            graph.addPass(name,
                          {{resource, Usage::eColorWrite}},
                          [&, resource, color](auto* cmd, auto& current)
                          {
                              beginColorPass(device, cmd, current.getTexture(resource).view(), {8, 8}, color.data());
                              device.core.CmdEndRendering(cmd);
                          });
        };
        const auto save = [&](const char* name, RenderGraph::Resource from, RenderGraph::Resource to)
        {
            graph.addPass(name,
                          {{from, Usage::eCopySource}, {to, Usage::eCopyDestination}},
                          [&, from, to](auto* cmd, auto& current)
                          {
                              copy(device, cmd, current.getTexture(from), current.getTexture(to));
                          });
            graph.exportResource(to);
        };
        clear("Green", overlapping, {0, 1, 0, 1});
        clear("Red", first, {1, 0, 0, 1});
        save("Save red", first, outputs[0]);
        clear("Blue", second, {0, 0, 1, 1});
        save("Save blue", second, outputs[1]);
        save("Save green", overlapping, outputs[2]);
        graph.compile(alias);
        const auto a = graph.resourceInfo(first);
        const auto b = graph.resourceInfo(second);
        require(a.lastUse < b.firstUse, "Transient resource intervals overlap");
        require((graph.getTexture(first).handle == graph.getTexture(second).handle) == alias,
                "Disjoint identical transients did not follow the allocation policy");
        require((a.allocation == b.allocation) == alias, "Observer allocation identities disagree");
        require(graph.getTexture(overlapping).handle != graph.getTexture(first).handle,
                "Overlapping live resources shared storage");
        require(graph.getTexture(outputs[0]).handle != graph.getTexture(first).handle,
                "Exported output shared transient storage");
        Frame frame(device);
        graph.execute(frame.begin());
        frame.submitAndWait();
        std::array<Image, 3> images;
        for (size_t i = 0; i < images.size(); ++i)
        {
            images[i] = readback(device, graph.getTexture(outputs[i]));
        }
        require(images[0].rgba[0] == 1 && images[1].rgba[2] == 1 && images[2].rgba[1] == 1,
                "Aliased storage corrupted pass outputs");
        if (alias)
        {
            bool rejected = false;
            try
            {
                graph.exportResource(first);
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
            require(rejected, "Editing an aliased plan invalidated its proven lifetimes");
            graph.execute(frame.begin());
            frame.submitAndWait();
            require(readback(device, graph.getTexture(outputs[0])).rgba == images[0].rgba,
                    "Rejected allocation-plan edit broke the valid graph");
        }
        return images;
    }
} // namespace

int main()
try
{
    vultra::Device device;
    history(device);
    const auto separate = transientReuse(device, false);
    const auto aliased  = transientReuse(device, true);
    for (size_t i = 0; i < separate.size(); ++i)
    {
        require(separate[i].rgba == aliased[i].rgba, "Aliasing changed the rendered image");
    }
    std::cout << "Graph history/reset and transient allocation parity passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
