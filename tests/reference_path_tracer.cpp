#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/builtin/environment_sampling.hpp>
#include <vultra/servers/rendering/builtin/reference_path_tracer.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <openpbr.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <random>
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

    double cellSolidAngle(uint32_t width, uint32_t height, uint32_t row)
    {
        const double pi = std::numbers::pi;
        return 2 * pi / width * (std::cos(pi * row / height) - std::cos(pi * (row + 1) / height));
    }

    glm::vec3 cellDirection(uint32_t width, uint32_t height, uint32_t x, uint32_t y)
    {
        const double phi   = ((x + 0.5) / width - 0.5) * 2 * std::numbers::pi;
        const double theta = (y + 0.5) / height * std::numbers::pi;
        return {float(std::cos(phi) * std::sin(theta)), float(std::cos(theta)), float(std::sin(phi) * std::sin(theta))};
    }

    void environmentDistribution()
    {
        using namespace vultra;
        Image        image {{16, 8}, std::vector<float>(16 * 8 * 4, 1)};
        const double uniform = 1 / (4 * std::numbers::pi);
        for (float level : {0.0f, 2.0f})
        {
            std::fill(image.rgba.begin(), image.rgba.end(), level);
            EnvironmentDistribution distribution(image);
            double                  integral = 0;
            for (uint32_t y = 0; y < image.size.height; ++y)
            {
                for (uint32_t x = 0; x < image.size.width; ++x)
                {
                    const auto pdf = distribution.pdf(cellDirection(16, 8, x, y));
                    require(std::abs(pdf - uniform) < 1e-7, "Constant/black environment is not uniform in solid angle");
                    integral += pdf * cellSolidAngle(16, 8, y);
                }
            }
            require(std::abs(integral - 1) < 1e-6, "Environment PDF does not integrate to one");
        }
        std::fill(image.rgba.begin(), image.rgba.end(), 0);
        const uint32_t sun = 3 * 16 + 7;
        for (size_t channel = 0; channel < 3; ++channel)
        {
            image.rgba[sun * 4 + channel] = 100;
        }
        EnvironmentDistribution distribution(image);
        std::mt19937            random(42);
        const auto              uniformRandom = [&]
        {
            return float(random() >> 8) * (1.0f / 16777216);
        };
        constexpr uint32_t samples           = 100000;
        uint32_t           brightSamples     = 0;
        double             estimatedIntegral = 0;
        for (uint32_t i = 0; i < samples; ++i)
        {
            const float technique = uniformRandom();
            const float cell      = uniformRandom();
            const float alias     = uniformRandom();
            const float longitude = uniformRandom();
            const float latitude  = uniformRandom();
            const auto  direction = distribution.sample(technique, cell, alias, longitude, latitude);
            require(std::abs(glm::length(direction) - 1) < 1e-6f, "Environment sample direction is not normalized");
            const auto pdf = distribution.pdf(direction);
            require(std::isfinite(pdf) && pdf >= uniform * 0.05 - 1e-8,
                    "Filtered dark texels have no sampling support");
            // Integrating the constant function with this nonuniform proposal must recover the sphere's area.
            estimatedIntegral += 1 / double(pdf);
            const double phi = std::atan2(direction.z, direction.x) / (2 * std::numbers::pi) + 0.5;
            const auto   x   = std::min(uint32_t(phi * 16), 15u);
            const auto   y =
                std::min(uint32_t(std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / std::numbers::pi * 8), 7u);
            brightSamples += y * 16 + x == sun;
        }
        const double expectedBright = 0.95 + 0.05 * cellSolidAngle(16, 8, 3) * uniform;
        require(std::abs(double(brightSamples) / samples - expectedBright) < 0.003,
                "Alias sampling frequencies disagree with independent cell probabilities");
        require(std::abs(estimatedIntegral / samples - 4 * std::numbers::pi) < 0.5,
                "Environment sampling/PDF pair changes an independently known integral");
        bool rejected = false;
        try
        {
            distribution.sample(0, 1, 0, 0, 0);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Out-of-range environment random input was accepted");
        image.rgba[0] = -1;
        rejected      = false;
        try
        {
            EnvironmentDistribution invalid(image);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Negative environment radiance was accepted");
    }

    OpenPBR_PreparedBsdf referenceBsdf(const vultra::SurfaceMaterial& material)
    {
        auto inputs                   = openpbr_make_default_resolved_inputs();
        inputs.base_color             = glm::vec3(material.baseColor);
        inputs.base_weight            = material.baseWeight;
        inputs.base_metalness         = material.baseMetalness;
        inputs.base_diffuse_roughness = material.baseDiffuseRoughness;
        inputs.specular_weight        = material.specularWeight;
        inputs.specular_color         = material.specularColor;
        inputs.specular_roughness     = material.specularRoughness;
        inputs.specular_ior           = material.specularIor;
        inputs.coat_weight            = material.coatWeight;
        inputs.coat_roughness         = material.coatRoughness;
        inputs.coat_ior               = material.coatIor;
        return openpbr_prepare(inputs,
                               glm::vec3(1),
                               OpenPBR_BaseRgbWavelengths_nm,
                               OpenPBR_VacuumIor,
                               glm::vec3(0, 0, 1));
    }

    glm::dvec3 environmentValue(const vultra::Image& image, glm::vec3 direction)
    {
        const double u      = std::atan2(direction.z, direction.x) / (2 * std::numbers::pi) + 0.5;
        const double v      = std::acos(std::clamp(double(direction.y), -1.0, 1.0)) / std::numbers::pi;
        const double x      = u * image.size.width - 0.5;
        const double y      = v * image.size.height - 0.5;
        const int    lowerX = int(std::floor(x));
        const int    lowerY = int(std::floor(y));
        const auto   texel  = [&](int column, int row)
        {
            column            = (column % int(image.size.width) + int(image.size.width)) % int(image.size.width);
            row               = std::clamp(row, 0, int(image.size.height) - 1);
            const auto offset = (size_t(row) * image.size.width + column) * 4;
            return glm::dvec3(image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2]);
        };
        return glm::mix(glm::mix(texel(lowerX, lowerY), texel(lowerX + 1, lowerY), x - lowerX),
                        glm::mix(texel(lowerX, lowerY + 1), texel(lowerX + 1, lowerY + 1), x - lowerX),
                        y - lowerY);
    }

    glm::dvec3 hemisphereIntegral(const OpenPBR_PreparedBsdf& bsdf, const vultra::Image* environment = nullptr)
    {
        // Independent midpoint quadrature over solid angle; no renderer RNG, BSDF sampler or MIS.
        constexpr uint32_t latitudes  = 256;
        constexpr uint32_t longitudes = 512;
        glm::dvec3         integral(0);
        for (uint32_t y = 0; y < latitudes; ++y)
        {
            const double cosine = (y + 0.5) / latitudes;
            const double sine   = std::sqrt(1 - cosine * cosine);
            for (uint32_t x = 0; x < longitudes; ++x)
            {
                const double    phi = (x + 0.5) / longitudes * 2 * std::numbers::pi;
                const glm::vec3 direction(float(sine * std::cos(phi)), float(sine * std::sin(phi)), float(cosine));
                const auto      radiance = environment ? environmentValue(*environment, direction) : glm::dvec3(1);
                integral += glm::dvec3(openpbr_get_sum_of_diffuse_specular(openpbr_eval(bsdf, direction))) * radiance;
            }
        }
        return integral * (2 * std::numbers::pi / (latitudes * longitudes));
    }

    void whiteFurnace(vultra::Device& device)
    {
        using namespace vultra;
        const auto file = std::filesystem::path("build/.tmp") / ("furnace-" + StableId::generate().toString() + ".hdr");
        std::ofstream stream(file, std::ios::binary);
        stream << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
        const unsigned char white[4] {128, 128, 128, 129};
        for (uint32_t i = 0; i < 8; ++i)
        {
            stream.write(reinterpret_cast<const char*>(white), 4);
        }
        stream.close();
        Environment environment(device, file);
        SceneData   data;
        data.vertices   = {{{-3, -3, 0}, {0, 0, 1}, {0, 0}},
                           {{3, -3, 0}, {0, 0, 1}, {1, 0}},
                           {{3, 3, 0}, {0, 0, 1}, {1, 1}},
                           {{-3, 3, 0}, {0, 0, 1}, {0, 1}}};
        data.indices    = {0, 1, 2, 0, 2, 3};
        data.primitives = {{0, 6, 0}};
        data.materials.emplace_back();
        data.materials[0].baseColor         = {0.6f, 0.3f, 0.1f, 1};
        data.materials[0].specularRoughness = 0.6f;
        data.materials[0].coatRoughness     = 0.4f;
        GpuScene            scene(device, data);
        ReferencePathTracer tracer(device, scene, environment);
        RenderGraph         graph(device);
        const auto          outputs = tracer.addPasses(graph, {8, 8});
        graph.exportResource(outputs.radiance);
        graph.compile();
        const RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                   glm::perspectiveRH_ZO(glm::radians(0.05f), 1.0f, 0.1f, 10.0f),
                                   0.1f,
                                   10};
        Frame              frame(device);
        Image              sun;
        for (uint32_t materialCase = 0; materialCase < 5; ++materialCase)
        {
            if (materialCase == 4)
            {
                std::ofstream changed(file, std::ios::binary | std::ios::trunc);
                changed << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
                for (uint32_t i = 0; i < 8; ++i)
                {
                    const std::array<unsigned char, 4> pixel = i == 2 ?
                                                                   std::array<unsigned char, 4> {128, 64, 32, 132} :
                                                                   std::array<unsigned char, 4> {0, 0, 0, 0};
                    changed.write(reinterpret_cast<const char*>(pixel.data()), pixel.size());
                }
                changed.close();
                environment.rebuild();
                sun = readback(device, *environment.radiance);
            }
            auto& material          = scene.materials[0];
            material.specularWeight = (materialCase == 0 || materialCase == 4) ? 0.0f : 1.0f;
            material.baseMetalness  = (materialCase >= 2 && materialCase < 4) ? 1.0f : 0.0f;
            material.coatWeight     = materialCase == 3 ? 0.7f : 0.0f;
            const auto bsdf         = referenceBsdf(material);
            const auto expected     = hemisphereIntegral(bsdf, materialCase == 4 ? &sun : nullptr);
            if (materialCase == 0)
            {
                require(glm::length(expected - glm::dvec3(0.6, 0.3, 0.1)) < 1e-6,
                        "Independent Lambertian furnace integral is incorrect");
            }
            graph.resetHistory();
            for (uint32_t i = 0; i < 512; ++i)
            {
                tracer.prepare(camera, graph, outputs, {}, 1, 71);
                graph.execute(frame.begin());
                frame.submitAndWait();
                tracer.completeFrame();
            }
            const auto image = readback(device, graph.getTexture(outputs.radiance));
            glm::dvec3 actual(0);
            for (size_t i = 0; i < image.rgba.size(); i += 4)
            {
                for (size_t channel = 0; channel < 3; ++channel)
                {
                    require(std::isfinite(image.rgba[i + channel]) && image.rgba[i + channel] >= 0,
                            "Furnace contains invalid radiance");
                    actual[channel] += image.rgba[i + channel] / 64;
                }
            }
            std::cout << "Furnace " << materialCase << ": CPU " << expected.x << "," << expected.y << "," << expected.z
                      << "; GPU " << actual.x << "," << actual.y << "," << actual.z << '\n';
            for (size_t channel = 0; channel < 3; ++channel)
            {
                require((materialCase == 4 || expected[channel] <= 1.001) &&
                            std::abs(actual[channel] - expected[channel]) < 0.015 + 0.02 * expected[channel],
                        "Slang transport differs from independent upstream OpenPBR hemispherical quadrature");
            }
        }
    }

} // namespace

int main()
try
{
    using namespace vultra;
    environmentDistribution();
    Device device(true, nullptr, VriFeature_RayQuery | VriFeature_Bindless);
    whiteFurnace(device);
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
