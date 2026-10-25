#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/scene_render_data.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#ifdef VULTRA_WITH_SHADER_COMPILER
#error "The runtime renderer test must not contain the shader compiler"
#endif
#if defined(VULTRA_WITH_SCENE_PREPARATION) || defined(VULTRA_WITH_TEXTURE_IMPORT)
#error "The runtime renderer test must consume prepared scene data"
#endif

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main(int argc, char** argv)
try
{
    using namespace vultra;
    if (argc != 3)
    {
        throw std::invalid_argument("Usage: module-rendering-runtime builtin.vpk fresh-output-directory");
    }
    const std::filesystem::path output(argv[2]);
    require(std::filesystem::create_directory(output), "Use a fresh output directory");
    // Read the entire package once; shader identities below have no corresponding filesystem files.
    const AssetSource fileSource(std::filesystem::absolute(argv[1]).parent_path());
    const auto        bytes = fileSource.read(std::filesystem::path(argv[1]).filename());
    const AssetSource shaders(VpkArchive("/embedded/builtin.vpk", bytes));
    Device            device;
    SceneData         scene;
    scene.vertices   = {{{-0.8f, -0.7f, 0}, {0, 0, 1}, {0, 0}},
                        {{0.8f, -0.7f, 0}, {0, 0, 1}, {1, 0}},
                        {{0, 0.8f, 0}, {0, 0, 1}, {0.5f, 1}}};
    scene.indices    = {0, 1, 2};
    scene.primitives = {{0, 3, 0}};
    scene.materials.emplace_back();
    scene.materials[0].baseColor = {0.7f, 0.2f, 0.05f, 1};
    scene.radius                 = 1;
    PreparedTextures prepared;
    TextureData      white;
    white.format = TextureFormat::eRgba8Unorm;
    white.levels = {{{1, 1}, std::vector<std::byte>(4, std::byte {255})}};
    prepared.images.push_back(std::move(white));
    prepared.materials.resize(1);
    GpuScene    gpu(device, scene, prepared);
    Environment environment(device, {}, nullptr, &shaders);
    environment.rebuild();
    BuiltinRenderer renderer(device, gpu, environment, VriFormat_RGBA8_UNORM, &shaders);
    renderer.settings.skybox           = false;
    renderer.settings.ibl              = false;
    renderer.settings.shadowFilter     = ShadowFilter::eDisabled;
    renderer.settings.shadowResolution = 32;
    renderer.settings.debugMode        = 1;
    constexpr Extent   size {129, 97};
    const RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0)),
                               glm::perspectiveRH_ZO(glm::radians(60.0f), float(size.width) / size.height, 0.1f, 10.0f),
                               0.1f,
                               10};
    Image              forward;
    for (const auto path : {RenderPath::eNaiveForward, RenderPath::eNaiveDeferred})
    {
        renderer.settings.path = path;
        RenderGraph graph(device);
        const auto  outputs = renderer.addPasses(graph, size);
        graph.exportResource(outputs.color);
        graph.exportResource(outputs.depth);
        if (path == RenderPath::eNaiveDeferred)
        {
            graph.exportResource(outputs.gbuffer[2]);
        }
        graph.compile();
        renderer.prepare(camera, graph, outputs);
        Frame frame(device);
        graph.execute(frame.begin());
        frame.submitAndWait();
        renderer.completeFrame();
        const auto image  = readback(device, graph.getTexture(outputs.color));
        const auto center = imagePixel(image, size.width / 2, size.height / 2);
        require(center[0] > center[1] + 0.1f && center[1] > center[2] + 0.1f,
                "Cooked renderer lost the triangle's material color");
        const auto depth = readback(device, graph.getTexture(outputs.depth));
        require(imagePixel(depth, size.width / 2, size.height / 2)[0] < 0.99f,
                "Cooked renderer did not write geometry depth");
        const bool deferred = path == RenderPath::eNaiveDeferred;
        savePng(image, output / (deferred ? "deferred.png" : "forward.png"));
        if (deferred)
        {
            float maximumError = 0;
            for (size_t i = 0; i < image.rgba.size(); ++i)
            {
                require(std::isfinite(image.rgba[i]), "Non-finite cooked renderer output");
                maximumError = std::max(maximumError, std::abs(image.rgba[i] - forward.rgba[i]));
            }
            require(maximumError < 0.01f, "Forward/deferred cooked renderer outputs differ");
            savePng(readback(device, graph.getTexture(outputs.gbuffer[2])), output / "albedo.png");
        }
        else
        {
            forward = image;
        }
    }
    std::cout << "Cooked forward/deferred renderer passed without host compilation or importers\n";
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
