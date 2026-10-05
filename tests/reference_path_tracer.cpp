#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/builtin/reference_path_tracer.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
{
    using namespace vultra;
    Device      device(true, nullptr, VriFeature_RayQuery | VriFeature_Bindless);
    Environment environment(device);
    SceneData   data;
    data.vertices   = {{{-3, -3, 0}, {0, 0, 1}, {0, 1}},
                       {{3, -3, 0}, {0, 0, 1}, {1, 1}},
                       {{3, 3, 0}, {0, 0, 1}, {1, 0}},
                       {{-3, 3, 0}, {0, 0, 1}, {0, 0}}};
    data.indices    = {0, 1, 2, 0, 2, 3};
    data.primitives = {{0, 6, 0}};
    data.materials.emplace_back();
    data.materials[0].emissionColor = {2, 1, 0.5f};
    data.images.push_back({2, 1, {255, 128, 64, 255, 128, 255, 64, 255}});
    data.materials[0].baseColorTexture.image = 0;
    data.materials[0].specularWeight         = 0;
    data.materials[0].doubleSided            = false;
    GpuScene            scene(device, data);
    ReferencePathTracer tracer(device, scene, environment);
    RenderGraph         graph(device);
    const auto          outputs = tracer.addPasses(graph, {17, 13});
    const std::array    aovs {outputs.radiance,
                              outputs.hdr,
                              outputs.albedo,
                              outputs.normal,
                              outputs.depth,
                              outputs.motion,
                              outputs.sampleCount,
                              outputs.rayCount};
    for (const auto resource : aovs)
    {
        graph.exportResource(resource);
    }
    graph.compile(true);
    const RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                               glm::perspectiveRH_ZO(glm::radians(45.0f), 17.0f / 13.0f, 0.1f, 10.0f),
                               0.1f,
                               10};
    Frame              frame(device);
    for (uint32_t i = 0; i < 4; ++i)
    {
        tracer.prepare(camera, graph, outputs, {}, 0, 42);
        graph.execute(frame.begin());
        frame.submitAndWait();
        tracer.completeFrame();
    }
    require(tracer.samples() == 4, "Reference sample count was not committed");
    for (const auto resource : aovs)
    {
        const auto image = readback(device, graph.getTexture(resource));
        for (float value : image.rgba)
        {
            require(std::isfinite(value), "Reference AOV contains a non-finite value");
        }
    }
    const auto image = readback(device, graph.getTexture(outputs.radiance));
    require(image.rgba[0] == 2 && image.rgba[1] == 1 && image.rgba[2] == 0.5f,
            "Reference transport changed isolated HDR emission");
    const auto normal = readback(device, graph.getTexture(outputs.normal));
    require(normal.rgba[2] == 1, "Reference normals lost orientation");
    const auto depth = readback(device, graph.getTexture(outputs.depth));
    require(std::abs(depth.rgba[0] - 3) < 1e-5f, "Reference camera view depth is incorrect");
    const auto count = readback(device, graph.getTexture(outputs.sampleCount));
    require(count.rgba[0] == 4, "Sample-count AOV differs from completed samples");
    const auto albedo = readback(device, graph.getTexture(outputs.albedo));
    require(albedo.rgba[0] != albedo.rgba[(size_t(13) * 17 - 1) * 4], "Reference material textures were ignored");
    graph.resetHistory();
    tracer.prepare(camera, graph, outputs, {}, 0, 42);
    graph.execute(frame.begin());
    frame.submitAndWait();
    tracer.completeFrame();
    require(tracer.samples() == 1, "Reference history did not reset");
    require(readback(device, graph.getTexture(outputs.radiance)).rgba == image.rgba,
            "Deterministic reset changed isolated emission");
    const auto render = [&](const RenderCamera&          selectedCamera,
                            std::span<const RenderLight> lights               = {},
                            float                        environmentIntensity = 0,
                            uint32_t                     seed                 = 42)
    {
        tracer.prepare(selectedCamera, graph, outputs, lights, environmentIntensity, seed);
        graph.execute(frame.begin());
        frame.submitAndWait();
        tracer.completeFrame();
        return readback(device, graph.getTexture(outputs.radiance));
    };
    auto orthographic       = camera;
    orthographic.projection = glm::orthoRH_ZO(-2.0f, 2.0f, -2.0f, 2.0f, camera.nearPlane, camera.farPlane);
    bool rejectedProjection = false;
    try
    {
        tracer.prepare(orthographic, graph, outputs, {}, 0, 42);
    }
    catch (const std::invalid_argument& error)
    {
        rejectedProjection = std::string_view(error.what()).find("pinhole perspective") != std::string_view::npos;
    }
    require(rejectedProjection, "Reference rendering accepted an unsupported ray-origin model");
    require(render(camera).rgba == image.rgba && tracer.samples() == 2,
            "Rejected camera projection changed accumulation or prevented the next valid frame");
    auto* radianceHandle = graph.getTexture(outputs.radiance).handle;
    scene.setPrimitiveTransforms(0, 1, glm::scale(glm::mat4(1), {-1, 1, 1}));
    require(render(camera).rgba == image.rgba && tracer.samples() == 1,
            "Mirrored single-sided geometry changed emission or failed to reset accumulation");
    require(readback(device, graph.getTexture(outputs.normal)).rgba[2] == 1,
            "Mirrored geometry flipped the authored world normal");
    scene.setPrimitiveTransforms(0,
                                 1,
                                 glm::translate(glm::mat4(1), {0.2f, 0, 0}) * glm::scale(glm::mat4(1), {-1, 1, 1}));
    render(camera);
    const auto objectMotion = readback(device, graph.getTexture(outputs.motion));
    require(objectMotion.rgba[0] < -0.5f && std::abs(objectMotion.rgba[1]) < 1e-5f && tracer.samples() == 1,
            "Rigid instance motion did not reproject the previous surface after a history reset");
    scene.setPrimitiveTransforms(0, 1, glm::scale(glm::mat4(1), {-1, 1, 1}));
    render(camera);
    scene.materials[0].alphaCutoff = 0.5f;
    scene.materials[0].baseColor.a = 0;
    const auto transparent         = render(camera);
    require(transparent.rgba[0] == 0 && readback(device, graph.getTexture(outputs.depth)).rgba[0] == 0,
            "Inline ray queries ignored the alpha mask");
    scene.materials[0].baseColor.a = 1;
    require(render(camera).rgba == image.rgba && tracer.samples() == 1,
            "A material edit did not restore visibility and reset history");
    scene.materials[0].emissionColor = {0, 0, 0};
    scene.materials[0].baseColor     = {0.5f, 0.5f, 0.5f, 1};
    const std::array light {RenderLight {.directionToLight = {0, 0, 1}, .intensity = 2}};
    const auto       lit       = render(camera, light);
    const auto       litAlbedo = readback(device, graph.getTexture(outputs.albedo));
    for (size_t i = 0; i < lit.rgba.size(); i += 4)
    {
        for (size_t channel = 0; channel < 3; ++channel)
        {
            const float expected = litAlbedo.rgba[i + channel] * 2 / std::numbers::pi_v<float>;
            require(std::abs(lit.rgba[i + channel] - expected) < 0.002f,
                    "Directional diffuse lighting disagrees with the analytic Lambertian solution");
        }
    }
    const std::array doubled {light[0], light[0]};
    const auto       twice = render(camera, doubled);
    require(std::abs(twice.rgba[0] - lit.rgba[0] * 2) < 0.002f && tracer.samples() == 1,
            "Uniform analytic-light selection lost its probability compensation");
    auto moving = camera;
    moving.view = glm::lookAtRH(glm::vec3(0.1f, 0, 3), glm::vec3(0.1f, 0, 0), glm::vec3(0, 1, 0));
    render(moving, doubled);
    require(tracer.samples() == 1, "Camera change retained reference accumulation");
    const auto motion = readback(device, graph.getTexture(outputs.motion));
    require(std::abs(motion.rgba[0]) > 0.1f && std::abs(motion.rgba[1]) < 1e-4f,
            "Camera motion did not reach the pixel-space motion AOV");
    render(moving, doubled, 0, 17);
    require(tracer.samples() == 1, "Seed change retained reference accumulation");
    render(moving, doubled, 0, 17);
    require(tracer.samples() == 2, "Stationary state unnecessarily reset accumulation");
    require(tracer.shader().reload(), "Reference shader could not reload");
    render(moving, doubled, 0, 17);
    require(tracer.samples() == 1 && graph.getTexture(outputs.radiance).handle == radianceHandle,
            "Shader replacement did not reset stable reference history");
    environment.setSource("resources/textures/environment_maps/citrus_orchard_puresky_1k.hdr");
    render(moving, doubled, 0, 17);
    require(tracer.samples() == 1, "Replacing an environment of different extent did not reset accumulation");

    const auto          cornell = loadObj("resources/models/CornellBox/CornellBox-Original.obj");
    GpuScene            cornellScene(device, cornell);
    ReferencePathTracer cornellTracer(device, cornellScene, environment);
    RenderGraph         cornellGraph(device);
    const Extent        cornellSize {24, 24};
    const auto          cornellOutputs = cornellTracer.addPasses(cornellGraph, cornellSize);
    cornellGraph.exportResource(cornellOutputs.radiance);
    cornellGraph.compile();
    const RenderCamera cornellCamera {glm::lookAtRH(glm::vec3(0, 1, 3.5f), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0)),
                                      glm::perspectiveRH_ZO(glm::radians(40.0f), 1.0f, 0.1f, 10.0f),
                                      0.1f,
                                      10};
    const auto         accumulate = [&](uint32_t samples, uint32_t seed)
    {
        cornellGraph.resetHistory();
        for (uint32_t i = 0; i < samples; ++i)
        {
            cornellTracer.prepare(cornellCamera, cornellGraph, cornellOutputs, {}, 0, seed);
            cornellGraph.execute(frame.begin());
            frame.submitAndWait();
            cornellTracer.completeFrame();
        }
        const auto result = readback(device, cornellGraph.getTexture(cornellOutputs.radiance));
        for (const float value : result.rgba)
        {
            require(std::isfinite(value) && value >= 0, "Cornell transport produced invalid radiance");
        }
        return result;
    };
    const auto low  = accumulate(16, 42);
    const auto high = accumulate(256, 42);
    require(accumulate(256, 42).rgba == high.rgba, "Fixed-seed Cornell accumulation was not deterministic");
    const auto reference = accumulate(2048, 71);
    const auto lowError  = compare(reference, low).mse;
    const auto highError = compare(reference, high).mse;
    require(highError < lowError * 0.5, "Cornell radiance did not converge as sample count increased");
    const auto different = accumulate(256, 43);
    require(different.rgba != high.rgba, "Reference random seed did not change the sample sequence");
    const auto directory = std::filesystem::path("build/.tmp") / ("reference-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    savePfm(low, directory / "cornell_16.pfm");
    savePfm(high, directory / "cornell_256.pfm");
    savePfm(reference, directory / "cornell_2048.pfm");
    std::cout << "Cornell MSE (16/256 samples against independent 2048): " << lowError << "/" << highError
              << "; captures: " << directory << '\n';
    std::cout << "Reference textured emission, analytic lighting, mirror/mask, motion, deterministic convergence "
                 "and state/shader resets passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
