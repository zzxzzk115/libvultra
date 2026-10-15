#ifdef TEST_SHADER
#include "../../examples/common/triangle.hpp"
#endif
#ifdef TEST_GRAPH_SHADER
#include "../../examples/research/color_gain.hpp"
#endif

#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/platform/os/process.hpp>

#ifdef TEST_GPU
#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#endif
#ifdef TEST_GRAPH
#include <vultra/servers/rendering/graph/pass_catalog.hpp>
#endif
#ifdef TEST_GRAPH_JSON
#include <vultra/servers/rendering/graph/graph_definition.hpp>
#endif
#ifdef TEST_DESKTOP
#include <vultra/main/app/desktop_app.hpp>
#endif
#ifdef TEST_IMAGE_IO
#include <vultra/core/image/image.hpp>
#endif
#ifdef TEST_SHADER
#include <vultra/drivers/rhi/shader_compiler.hpp>
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

#ifdef TEST_GPU
    void gpu()
    {
        Device                        device;
        const std::array<uint32_t, 3> values {0x12345678, 17, 0xfedcba98};
        auto                          source = uploadBuffer(device,
                                                            std::as_bytes(std::span(values)),
                                                            VriBufferUsage_TransferSrc,
                                                            {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        Buffer   result(device, {sizeof(values), 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        Texture  color(device, colorTexture({17, 13}, VriFormat_RGBA32_SFLOAT));
        Profiler profiler(device);
        Frame    frame(device);
        for (unsigned index = 0; index < 3; ++index)
        {
            auto* cmd = frame.begin();
            profiler.beginFrame(cmd);
            profiler.beginPass(cmd, "Probe");
            result.transition(cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            const VriBufferCopyDesc copy {0, 0, sizeof(values)};
            device.core.CmdCopyBuffer(cmd, result.handle, source->handle, &copy);
            color.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const std::array<float, 4> expected {float(index) * 0.25f, 0.5f, 0.75f, 1};
            beginColorPass(device, cmd, color.view(), {17, 13}, expected.data());
            device.core.CmdEndRendering(cmd);
            profiler.endPass(cmd);
            profiler.resolve(cmd);
            frame.submitAndWait();
            profiler.collect();
            auto* mapped = device.core.MapBuffer(result.handle, 0, sizeof(values));
            require(mapped != nullptr, "GPU result is not readable");
            const bool copied = std::memcmp(mapped, values.data(), sizeof(values)) == 0;
            device.core.UnmapBuffer(result.handle);
            require(copied, "GPU upload/copy/readback changed the data");
            const auto image = readback(device, color);
            for (uint32_t y = 0; y < image.size.height; ++y)
            {
                for (uint32_t x = 0; x < image.size.width; ++x)
                {
                    require(imagePixel(image, x, y) == expected, "GPU image result differs from recorded commands");
                }
            }
            require(profiler.timings().size() == 1 && profiler.timings().front().cpuMs >= 0,
                    "Completed GPU frame did not publish timing");
        }
    }
#endif

#ifdef TEST_SHADER
    void shader(const std::filesystem::path& directory)
    {
        Device               device;
        ShaderCompileOptions options;
        options.includeDirectories = {"builtin/shaders", "examples/common"};
        ShaderCompiler compiler(directory / "shader-cache");
        const auto     program = compiler.compile("examples/research/shaders/triangle.slang", options);
        const auto     cooked  = directory / "triangle.vshaderc";
        program.save(cooked);
        const auto restored = ShaderProgram::load(cooked);
        require(restored.shaders.size() == program.shaders.size(), "Shader archive lost an entry");
        for (size_t index = 0; index < program.shaders.size(); ++index)
        {
            require(restored.shaders[index].words == program.shaders[index].words,
                    "Shader archive changed compiled bytecode");
        }
        Triangle triangle(device, VriFormat_RGBA8_UNORM, cooked);
        Texture  color(device, colorTexture({65, 65}));
        Frame    frame(device);
        auto*    cmd = frame.begin();
        color.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[] {0, 0, 0, 1};
        triangle.draw(cmd, color, clear);
        frame.submitAndWait();
        const auto image  = readback(device, color);
        const auto center = imagePixel(image, 32, 32);
        require(center[0] + center[1] + center[2] > 0.2f, "Minimal shader triangle did not render");
        require(imagePixel(image, 0, 0) == std::array<float, 4> {0, 0, 0, 1},
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

        std::vector<RenderGraph::Resource> addPasses(RenderGraph&     graph,
                                                     std::string_view name,
                                                     std::span<const RenderGraph::Resource>,
                                                     std::span<const double> parameters) override
        {
            const auto color =
                graph.createTexture(std::string(name) + ".color", colorTexture({11, 7}, kGraphColorFormat));
            graph.addPass(std::string(name),
                          {{color, Usage::eColorWrite}},
                          [this, color, parameters](auto* cmd, auto& active)
                          {
                              const float clear[] {float(parameters[0]), 0, 0, 1};
                              beginColorPass(m_Device, cmd, active.getTexture(color).view(), {11, 7}, clear);
                              m_Device.core.CmdEndRendering(cmd);
                          });
            return {color};
        }

    private:
        Device& m_Device;
    };

    void graph(const std::filesystem::path& directory)
    {
        Device      device;
        PassCatalog catalog(device);
        require(catalog.definitions().empty(), "Project-only graph acquired built-in renderer passes");
        catalog.add({"probe.clear",
                     {},
                     {{"color", PassResourceKind::eTexture, kGraphColorFormat}},
                     {{"red", 0.2, 0, 1}},
                     0,
                     [](Device& owner)
                     {
                         return std::make_unique<ClearPass>(owner);
                     }});
        BuiltPass pass;
#ifdef TEST_GRAPH_SHADER
        catalog.add(research::colorGainDefinition());
        BuiltPass gain;
#endif
#ifdef TEST_GRAPH_JSON
        GraphBuild definitionPasses;
#endif
        RenderGraph           graph(device);
        RenderGraph::Resource output;
#ifdef TEST_GRAPH_JSON
#ifdef TEST_GRAPH_SHADER
        const auto definition = GraphDefinition::parse(
            R"({
                "format": "vultra.graph", "version": 1,
                "passes": [
                    {"id": "clear", "type": "probe.clear", "parameters": {"red": 0.2}},
                    {"id": "gain", "type": "research.color_gain", "parameters": {"gain": 3}}
                ],
                "edges": [{"from": "clear.color", "to": "gain.source"}],
                "outputs": ["gain.color"]
            })");
#else
        const auto definition = GraphDefinition::parse(
            R"({
                "format": "vultra.graph", "version": 1,
                "passes": [{"id": "clear", "type": "probe.clear", "parameters": {"red": 0.2}}],
                "edges": [], "outputs": ["clear.color"]
            })");
#endif
        definitionPasses = definition.build(graph, catalog, {});
        output           = definitionPasses.outputs.front().resource;
#else
        pass   = catalog.build(graph, "probe.clear", "clear", {});
        output = pass.outputs.front();
#ifdef TEST_GRAPH_SHADER
        gain   = catalog.build(graph, "research.color_gain", "gain", pass.outputs, {{"gain", 3}});
        output = gain.outputs.front();
#endif
#endif
        graph.exportResource(output);
        graph.compile();
        Frame frame(device);
        graph.execute(frame.begin());
        frame.submitAndWait();
        const auto image = readback(device, graph.getTexture(output));
#ifdef TEST_GRAPH_SHADER
        for (uint32_t y = 0; y < image.size.height; ++y)
        {
            for (uint32_t x = 0; x < image.size.width; ++x)
            {
                const auto pixel = imagePixel(image, x, y);
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
        DesktopProbe() :
            DesktopApp({.title = "Vultra module probe", .size = {320, 240}})
        {
        }

    private:
        void onRender(VriCommandBuffer* cmd, Texture& target) override
        {
            target.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[] {0.2f, 0.4f, 0.6f, 1};
            beginColorPass(getDevice(), cmd, target.view(), getSwapchain().size(), clear);
            getDevice().core.CmdEndRendering(cmd);
        }
    };
#endif
} // namespace

int main(int argc, char** argv)
try
{
    using namespace vultra;
    Logger::configure();
    require(argc == 2, "Pass an unused output directory to the module probe");
    const std::filesystem::path directory(argv[1]);
    require(!std::filesystem::exists(directory), "Module probe output directory already exists");
    std::filesystem::create_directories(directory);
    const std::string expected = "finite experiment\n";
    writeFileAtomically(directory / "result.txt", std::as_bytes(std::span(expected)));
    std::ifstream     input(directory / "result.txt");
    const std::string actual((std::istreambuf_iterator<char>(input)), {});
    require(actual == expected && !executablePath().empty(), "Foundation file/process APIs failed");
#ifdef TEST_GPU
    gpu();
#endif
#ifdef TEST_GRAPH
    graph(directory);
#endif
#ifdef TEST_SHADER
    shader(directory);
#endif
#ifdef TEST_IMAGE_IO
    const Image image {{2, 2}, {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1}};
    savePng(image, directory / "image.png");
    require(loadPng(directory / "image.png").rgba == image.rgba, "Optional image codec round trip failed");
    savePfm(image, directory / "image.pfm");
#endif
#ifdef TEST_DESKTOP
    if (std::getenv("VULTRA_MODULE_TEST_WINDOW"))
    {
        DesktopProbe desktop;
        desktop.run(2);
    }
#endif
    Logger::app().info("Selected module APIs passed");
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
