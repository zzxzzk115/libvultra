#include "../support/check.hpp"
#ifdef TEST_SHADER
#include "../../examples/common/triangle.hpp"
#endif
#ifdef TEST_GRAPH_SHADER
#include "../../examples/offline/color_gain.hpp"
#endif

#include <vultra/core/base/logger.hpp>
#include <vultra/core/image/image.hpp>

#ifdef TEST_PLATFORM
#include <vultra/platform/os/file.hpp>
#include <vultra/platform/os/process.hpp>
#endif
#ifdef TEST_GPU
#include <vultra/gpu/buffer_upload.hpp>
#include <vultra/gpu/image_readback.hpp>
#include <vultra/gpu/profiling/profiler.hpp>
#endif
#ifdef TEST_GRAPH
#include <vultra/graph/pass_catalog.hpp>
#endif
#ifdef TEST_GRAPH_JSON
#include <vultra/graph/graph_definition.hpp>
#endif
#ifdef TEST_DESKTOP
#include <vultra/app/desktop_app.hpp>
#endif
#ifdef TEST_SHADER
#include <vultra/shader/shader_compiler.hpp>
#endif

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace vultra;
    using support::must;
    using support::require;

#ifdef TEST_GPU
    void gpu()
    {
        const auto                    device = must(Device::create());
        const std::array<uint32_t, 3> values {0x12345678, 17, 0xfedcba98};
        const auto source = must(uploadBuffer(*device,
                                              std::as_bytes(std::span(values)),
                                              VriBufferUsage_TransferSrc,
                                              {VriAccess_CopySourceRead, VriPipelineStage_Transfer}));
        auto       result = must(
            Buffer::create(*device, {sizeof(values), 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback}));
        auto       color    = must(Texture::create(*device, colorTexture({17, 13}, VriFormat_RGBA32_SFLOAT)));
        const auto profiler = must(Profiler::create(*device));
        const auto frame    = must(Frame::create(*device));
        for (unsigned index = 0; index < 3; ++index)
        {
            auto* cmd = must(frame->begin());
            profiler->beginFrame(cmd);
            must(profiler->beginPass(cmd, "Probe"));
            result.transition(cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            const VriBufferCopyDesc copy {0, 0, sizeof(values)};
            device->core.CmdCopyBuffer(cmd, result.handle, source.handle, &copy);
            color.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const std::array<float, 4> expected {float(index) * 0.25f, 0.5f, 0.75f, 1};
            beginColorPass(*device, cmd, must(color.view()), {17, 13}, expected.data());
            device->core.CmdEndRendering(cmd);
            must(profiler->endPass(cmd));
            must(profiler->resolve(cmd));
            must(frame->submitAndWait());
            must(profiler->collect());
            auto* mapped = device->core.MapBuffer(result.handle, 0, sizeof(values));
            require(mapped != nullptr, "GPU result is not readable");
            const bool copied = std::memcmp(mapped, values.data(), sizeof(values)) == 0;
            device->core.UnmapBuffer(result.handle);
            require(copied, "GPU upload/copy/readback changed the data");
            const auto image = must(readback(*device, color));
            for (uint32_t y = 0; y < image.size.height; ++y)
            {
                for (uint32_t x = 0; x < image.size.width; ++x)
                {
                    require(must(imagePixel(image, x, y)) == expected,
                            "GPU image result differs from recorded commands");
                }
            }
            require(profiler->timings().size() == 1 && profiler->timings().front().cpuMs >= 0,
                    "Completed GPU frame did not publish timing");
        }
    }
#endif

#ifdef TEST_SHADER
    void shader(const std::filesystem::path& directory)
    {
        const auto           device = must(Device::create());
        ShaderCompileOptions options;
        options.includeDirectories = {"builtin/shaders", "examples/common"};
        auto       compiler        = must(ShaderCompiler::create(directory / "shader-cache"));
        const auto program         = must(compiler.compile("examples/offline/shaders/triangle.slang", options));
        const auto cooked          = directory / "triangle.vshaderc";
        must(program.save(cooked));
        const auto restored = must(ShaderProgram::load(cooked));
        require(restored.shaders.size() == program.shaders.size(), "Shader archive lost an entry");
        for (size_t index = 0; index < program.shaders.size(); ++index)
        {
            require(restored.shaders[index].words == program.shaders[index].words,
                    "Shader archive changed compiled bytecode");
        }
        const auto triangle = must(Triangle::create(*device, VriFormat_RGBA8_UNORM, cooked));
        auto       color    = must(Texture::create(*device, colorTexture({65, 65})));
        const auto frame    = must(Frame::create(*device));
        auto*      cmd      = must(frame->begin());
        color.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[] {0, 0, 0, 1};
        must(triangle->draw(cmd, color, clear));
        must(frame->submitAndWait());
        const auto image  = must(readback(*device, color));
        const auto center = must(imagePixel(image, 32, 32));
        require(center[0] + center[1] + center[2] > 0.2f, "Minimal shader triangle did not render");
        require(must(imagePixel(image, 0, 0)) == std::array<float, 4> {0, 0, 0, 1},
                "Minimal shader triangle changed the background");
    }
#endif

#ifdef TEST_GRAPH
#ifdef TEST_GRAPH_SHADER
    constexpr VriFormat kGraphColorFormat = VriFormat_RGBA16_SFLOAT;
#else
    constexpr VriFormat kGraphColorFormat = VriFormat_RGBA8_UNORM;
#endif
    class ClearPass final : public GraphPass
    {
    public:
        explicit ClearPass(Device& device) :
            m_Device(device)
        {
        }

        std::expected<std::vector<RenderGraph::Resource>, Error> addPasses(RenderGraph&     graph,
                                                                           std::string_view name,
                                                                           std::span<const RenderGraph::Resource>,
                                                                           std::span<const double> parameters) override
        {
            const auto color =
                graph.createTexture(std::string(name) + ".color", colorTexture({11, 7}, kGraphColorFormat));
            if (!color)
            {
                return std::unexpected(color.error());
            }
            auto added =
                graph.addPass(std::string(name),
                              {{*color, Usage::eColorWrite}},
                              [this, color = *color, parameters](auto* cmd, auto& active) -> std::expected<void, Error>
                              {
                                  const auto view = active.getTexture(color).view();
                                  if (!view)
                                  {
                                      return std::unexpected(view.error());
                                  }
                                  const float clear[] {float(parameters[0]), 0, 0, 1};
                                  beginColorPass(m_Device, cmd, *view, {11, 7}, clear);
                                  m_Device.core.CmdEndRendering(cmd);
                                  return {};
                              });
            if (!added)
            {
                return std::unexpected(added.error());
            }
            return std::vector {*color};
        }

    private:
        Device& m_Device;
    };

    void graph(const std::filesystem::path& directory)
    {
        const auto  device = must(Device::create());
        PassCatalog catalog(*device);
        require(catalog.definitions().empty(), "Project-only graph acquired built-in renderer passes");
        must(catalog.add({"probe.clear",
                          {},
                          {{"color", PassResourceKind::eTexture, kGraphColorFormat}},
                          {{"red", 0.2, 0, 1}},
                          0,
                          [](Device& owner) -> std::expected<std::unique_ptr<GraphPass>, Error>
                          {
                              return std::make_unique<ClearPass>(owner);
                          }}));
        BuiltPass pass;
#ifdef TEST_GRAPH_SHADER
        must(catalog.add(research::colorGainDefinition()));
        BuiltPass gain;
#endif
#ifdef TEST_GRAPH_JSON
        GraphBuild definitionPasses;
#endif
        RenderGraph           graph(*device);
        RenderGraph::Resource output;
#ifdef TEST_GRAPH_JSON
#ifdef TEST_GRAPH_SHADER
        const auto definition = must(GraphDefinition::parse(
            R"({
                "format": "vultra.graph", "version": 1,
                "passes": [
                    {"id": "clear", "type": "probe.clear", "parameters": {"red": 0.2}},
                    {"id": "gain", "type": "research.color_gain", "parameters": {"gain": 3}}
                ],
                "edges": [{"from": "clear.color", "to": "gain.source"}],
                "outputs": ["gain.color"]
            })"));
#else
        const auto definition = must(GraphDefinition::parse(
            R"({
                "format": "vultra.graph", "version": 1,
                "passes": [{"id": "clear", "type": "probe.clear", "parameters": {"red": 0.2}}],
                "edges": [], "outputs": ["clear.color"]
            })"));
#endif
        definitionPasses = must(definition.build(graph, catalog, {}));
        output           = definitionPasses.outputs.front().resource;
#else
        pass   = must(catalog.build(graph, "probe.clear", "clear", {}));
        output = pass.outputs.front();
#ifdef TEST_GRAPH_SHADER
        gain   = must(catalog.build(graph, "research.color_gain", "gain", pass.outputs, {{"gain", 3}}));
        output = gain.outputs.front();
#endif
#endif
        must(graph.exportResource(output));
        must(graph.compile());
        const auto frame = must(Frame::create(*device));
        must(graph.execute(must(frame->begin())));
        must(frame->submitAndWait());
        const auto image = must(readback(*device, graph.getTexture(output)));
#ifdef TEST_GRAPH_SHADER
        for (uint32_t y = 0; y < image.size.height; ++y)
        {
            for (uint32_t x = 0; x < image.size.width; ++x)
            {
                const auto pixel = must(imagePixel(image, x, y));
                require(std::abs(pixel[0] - 0.6f) < 1e-3f && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 1,
                        "Minimal compute graph produced an incorrect pixel");
            }
        }
        std::ofstream result(directory / "compute.rgba", std::ios::binary);
        result.write(reinterpret_cast<const char*>(image.rgba.data()),
                     std::streamsize(image.rgba.size() * sizeof(float)));
        require(bool(result), "Cannot save compute graph readback");
#else
        static_cast<void>(directory);
        require(std::abs(image.rgba.front() - 0.2f) < 1e-6f, "Project pass did not produce its requested value");
#endif
    }
#endif

#ifdef TEST_DESKTOP
    // Link the actual desktop lifecycle while keeping automated execution off the user's desktop.
    class DesktopProbe final : public DesktopApp
    {
    public:
        static inline const AppConfig kConfig {.title = "Vultra module probe", .size = {320, 240}};

        explicit DesktopProbe(Services services) :
            DesktopApp(std::move(services), kConfig)
        {
        }

    private:
        std::expected<void, Error> onRender(VriCommandBuffer* cmd, Texture& target) override
        {
            target.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[] {0.2f, 0.4f, 0.6f, 1};
            const auto  view = target.view();
            if (!view)
            {
                return std::unexpected(view.error());
            }
            beginColorPass(getDevice(), cmd, *view, getSwapchain().size(), clear);
            getDevice().core.CmdEndRendering(cmd);
            return {};
        }
    };
#endif
} // namespace

int main(int argc, char** argv)
{
    using namespace vultra;
    must(Logger::configure());
    require(argc == 2, "Pass an unused output directory to the module probe");
    const std::filesystem::path directory(argv[1]);
    require(!std::filesystem::exists(directory), "Module probe output directory already exists");
    std::filesystem::create_directories(directory);
#ifdef TEST_PLATFORM
    const std::string expected = "finite experiment\n";
    must(writeFileAtomically(directory / "result.txt", std::as_bytes(std::span(expected))));
    std::ifstream     input(directory / "result.txt");
    const std::string actual((std::istreambuf_iterator<char>(input)), {});
    require(actual == expected && !must(executablePath()).empty(), "Platform file/process APIs failed");
#endif
#ifdef TEST_GPU
    gpu();
#endif
#ifdef TEST_GRAPH
    graph(directory);
#endif
#ifdef TEST_SHADER
    shader(directory);
#endif
    const Image image {{2, 2}, {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1}};
    must(savePng(image, directory / "image.png"));
    require(must(loadPng(directory / "image.png")).rgba == image.rgba, "Image codec round trip failed");
    must(savePfm(image, directory / "image.pfm"));
#ifdef TEST_DESKTOP
    if (std::getenv("VULTRA_MODULE_TEST_WINDOW"))
    {
        DesktopProbe desktop(must(DesktopApp::createServices(DesktopProbe::kConfig)));
        must(desktop.run(2));
    }
#endif
    Logger::app().info("Selected module APIs passed");
    return 0;
}
