#include <vultra/function/asset/asset_pipeline.hpp>
#include <vultra/function/renderer/builtin/builtin_renderer.hpp>
#include <vultra/function/renderer/texture_blit.hpp>
#include <vultra/function/research/capture.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <openpbr.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void finite(const vultra::Image& image)
    {
        for (float channel : image.rgba)
        {
            require(std::isfinite(channel) && channel >= 0, "Non-finite or negative renderer output");
        }
    }

    void constantEnvironment(vultra::Device& device, vultra::Environment& environment)
    {
        auto constant = [](const vultra::Image& image)
        {
            finite(image);
            for (size_t pixel = 0; pixel < image.rgba.size(); pixel += 4)
            {
                require(std::abs(image.rgba[pixel] - 1) < 0.004f && std::abs(image.rgba[pixel + 1] - 0.5f) < 0.004f &&
                            std::abs(image.rgba[pixel + 2] - 0.25f) < 0.004f,
                        "IBL must preserve constant radiance");
            }
        };
        constant(vultra::readback(device, *environment.radiance));
        constant(vultra::readback(device, *environment.diffuse));
        for (uint32_t mip : {0u, 4u, 8u})
        {
            constant(vultra::readback(device, *environment.specular, mip));
        }
        const auto lut = vultra::readback(device, *environment.brdfLut);
        finite(lut);
        const size_t mirror = (size_t(1) * lut.size.width + lut.size.width - 2) * 4;
        std::cout << "LUT mirror: " << lut.rgba[mirror] << ", " << lut.rgba[mirror + 1] << std::endl;
        require(lut.rgba[mirror] > 0.95f && lut.rgba[mirror + 1] < 0.01f, "BRDF LUT mirror limit");
    }

    vultra::Image render(vultra::Device&                         device,
                         vultra::BuiltinRenderer&                renderer,
                         vultra::RenderGraph&                    graph,
                         const vultra::BuiltinRenderer::Outputs& outputs,
                         const vultra::RenderCamera&             camera)
    {
        renderer.prepare(camera, graph, outputs);
        vultra::Frame frame(device);
        auto*         cmd = frame.begin();
        graph.execute(cmd);
        frame.submitAndWait();
        auto image = vultra::readback(device, graph.getTexture(outputs.hdr));
        finite(image);
        return image;
    }

    // Compare the upstream C++ backend against Vultra's material mapping and Slang backend.
    glm::vec3 referenceBrdf(const vultra::SurfaceMaterial& material, glm::vec3 view, glm::vec3 light)
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
        const auto prepared =
            openpbr_prepare(inputs, glm::vec3(1), OpenPBR_BaseRgbWavelengths_nm, OpenPBR_VacuumIor, view);
        // Compare OpenPBR scattering; glTF emission is added independently of its coating lobes.
        return openpbr_get_sum_of_diffuse_specular(openpbr_eval(prepared, light)) +
               material.emissionColor * material.emissionLuminance;
    }

    void materialReference(vultra::Device& device, vultra::Environment& environment)
    {
        vultra::Scene scene;
        scene.vertices   = {{{-2, -2, 0}, {0, 0, 1}, {0, 0}},
                            {{2, -2, 0}, {0, 0, 1}, {1, 0}},
                            {{2, 2, 0}, {0, 0, 1}, {1, 1}},
                            {{-2, 2, 0}, {0, 0, 1}, {0, 1}}};
        scene.indices    = {0, 1, 2, 0, 2, 3};
        scene.primitives = {{0, 6, 0}};
        scene.materials.emplace_back();
        scene.radius = 3;
        vultra::GpuScene        gpu(device, scene);
        vultra::BuiltinRenderer renderer(device, gpu, environment);
        renderer.settings.ibl              = false;
        renderer.settings.shadowFilter     = vultra::ShadowFilter::eDisabled;
        renderer.settings.lightIntensity   = 1;
        renderer.settings.lightColor       = {1, 1, 1};
        renderer.settings.directionToLight = {0.4f, 0.6f, 1};
        renderer.settings.shadowResolution = 64;
        vultra::RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                     glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                     0.1f,
                                     10};
        vultra::RenderGraph  graph(device);
        const auto           outputs = renderer.addPasses(graph, {65, 65});
        graph.exportResource(outputs.color);
        graph.compile();
        require(graph.activePasses().size() == 7, "Expected four shadow, skybox, forward and tone-mapping passes");
        size_t caseCount = 0;
        float  maxError  = 0;
        auto   compare   = [&](glm::vec3 view)
        {
            camera.view      = glm::lookAtRH(view * 3.0f, glm::vec3(0), glm::vec3(0, 1, 0));
            const auto image = render(device, renderer, graph, outputs, camera);
            const auto expected =
                referenceBrdf(gpu.materials[0], view, glm::normalize(renderer.settings.directionToLight));
            const size_t center = (32 * 65 + 32) * 4;
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                const float error = std::abs(image.rgba[center + channel] - expected[channel]);
                maxError          = std::max(maxError, error);
                if (error > 0.003f + std::abs(expected[channel]) * 0.002f)
                {
                    std::cerr << "OpenPBR case " << caseCount << ", channel " << channel
                              << ": Slang=" << image.rgba[center + channel] << ", C++=" << expected[channel] << '\n';
                    throw std::runtime_error("Slang OpenPBR differs from the upstream C++ backend");
                }
            }
            ++caseCount;
        };
        for (float metalness : {0.0f, 0.4f, 1.0f})
        {
            for (float coat : {0.0f, 0.6f})
            {
                for (uint32_t variant = 0; variant < 4; ++variant)
                {
                    auto& material                     = gpu.materials[0];
                    material                           = {};
                    material.baseColor                 = {0.6f, 0.2f, 0.1f, 1};
                    material.baseMetalness             = metalness;
                    material.specularRoughness         = 0.35f;
                    material.specularIor               = 1.4f;
                    material.specularWeight            = 0.8f;
                    material.coatWeight                = coat;
                    material.coatRoughness             = 0.2f;
                    renderer.settings.directionToLight = {0.4f, 0.6f, 1};
                    if (variant == 1)
                    {
                        material.baseWeight           = 0.4f;
                        material.baseDiffuseRoughness = 0.7f;
                        material.specularRoughness    = 0.8f;
                        material.specularColor        = {0.6f, 1, 0.7f};
                    }
                    else if (variant == 2)
                    {
                        material.specularWeight            = 0;
                        material.specularRoughness         = 0.08f;
                        renderer.settings.directionToLight = {1, 0.5f, 0.1f};
                    }
                    else if (variant == 3)
                    {
                        material.specularRoughness = 0.08f;
                        material.emissionColor     = {0.4f, 0.1f, 0.05f};
                        material.emissionLuminance = 2;
                        material.coatIor           = 1.8f;
                    }
                    compare({0, 0, 1});
                    compare(glm::normalize(glm::vec3(1, 0, 0.4f)));
                }
            }
        }
        gpu.materials[0]                   = {};
        renderer.settings.directionToLight = {0.4f, 0.6f, -1};
        compare({0, 0, 1});
        std::cout << "OpenPBR Slang/C++: " << caseCount << " cases, max error=" << maxError << '\n';

        vultra::BuiltinRenderer linearRenderer(device, gpu, environment, VriFormat_RGBA16_SFLOAT);
        linearRenderer.settings = renderer.settings;
        vultra::RenderGraph linearGraph(device);
        const auto          linearOutputs = linearRenderer.addPasses(linearGraph, {65, 65});
        linearGraph.exportResource(linearOutputs.color);
        linearGraph.compile();
        vultra::Texture     mirror(device, vultra::colorTexture({65, 65}));
        vultra::TextureBlit blit(device, mirror.desc.format);
        blit.setSource(0, linearGraph.getTexture(linearOutputs.color));
        gpu.materials[0].emissionColor     = {0.02f, 0.3f, 2};
        gpu.materials[0].emissionLuminance = 1;
        for (float exposure : {-2.0f, 0.0f, 2.0f})
        {
            renderer.settings.exposure       = exposure;
            linearRenderer.settings.exposure = exposure;
            render(device, renderer, graph, outputs, camera);
            const auto desktop = vultra::readback(device, graph.getTexture(outputs.color));
            render(device, linearRenderer, linearGraph, linearOutputs, camera);
            vultra::Frame frame(device);
            auto*         cmd = frame.begin();
            blit.draw(cmd, mirror, {0, 0, 65, 65}, 0, true);
            frame.submitAndWait();
            const auto mirrored = vultra::readback(device, mirror);
            for (size_t i = 0; i < desktop.rgba.size(); ++i)
            {
                require(std::abs(desktop.rgba[i] - mirrored.rgba[i]) < 0.008f,
                        "Linear XR tone mapping plus mirror encoding differs from desktop output");
            }
        }
        std::cout << "Linear XR tone mapping and display-encoded mirror match desktop output\n";
    }

    void materialTextures(vultra::Device& device, vultra::Environment& environment, VriFormat normalFormat)
    {
        vultra::ImportedAsset asset;
        auto&                 scene = asset.scene;
        scene.vertices              = {{{-2, -2, 0}, {0, 0, 1}, {0, 0}},
                                       {{2, -2, 0}, {0, 0, 1}, {1, 0}},
                                       {{2, 2, 0}, {0, 0, 1}, {1, 1}},
                                       {{-2, 2, 0}, {0, 0, 1}, {0, 1}}};
        scene.indices               = {0, 1, 2, 0, 2, 3};
        scene.primitives            = {{0, 6, 0}};
        scene.materials.emplace_back();
        scene.images.push_back({1, 1, {64, 128, 192, 102}});
        auto& material              = scene.materials[0];
        material.baseColor          = {0.6f, 0.2f, 0.1f, 1};
        material.specularWeight     = 0.8f;
        material.specularColor      = {0.9f, 0.7f, 0.5f};
        material.specularImage      = 0;
        material.specularColorImage = 0;
        scene.radius                = 3;
        asset.textures = vultra::prepareTextures(scene, {.compression = vultra::TextureCompression::eNone});
        // A constant BC5 block with X=Y=128/255; Z must be reconstructed by the shader.
        std::vector<std::byte> block(16);
        block[0] = block[1] = block[8] = block[9] = std::byte(128);
        const auto normalSlot                     = uint32_t(asset.textures.images.size());
        if (normalFormat == VriFormat_RG8_UNORM)
        {
            block.assign(4 * 4 * 2, std::byte(128));
        }
        asset.textures.images.push_back({normalFormat, {{{4, 4}, std::move(block)}}});
        asset.textures.materials[0][2] = normalSlot;
        vultra::GpuScene        gpu(device, asset);
        vultra::BuiltinRenderer renderer(device, gpu, environment);
        renderer.settings.ibl              = false;
        renderer.settings.skybox           = false;
        renderer.settings.shadowFilter     = vultra::ShadowFilter::eDisabled;
        renderer.settings.lightIntensity   = 1;
        renderer.settings.lightColor       = {1, 1, 1};
        renderer.settings.directionToLight = {0.4f, 0.6f, 1};
        renderer.settings.shadowResolution = 64;
        const vultra::RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                           glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                           0.1f,
                                           10};
        vultra::RenderGraph        graph(device);
        const auto                 outputs = renderer.addPasses(graph, {65, 65});
        graph.exportResource(outputs.color);
        graph.compile();
        auto effective = material;
        effective.specularWeight *= 102.0f / 255;
        effective.specularColor *= glm::vec3(0.05126946f, 0.2158605f, 0.5271151f);
        const auto   expected = referenceBrdf(effective, {0, 0, 1}, glm::normalize(renderer.settings.directionToLight));
        const size_t center   = (32 * 65 + 32) * 4;
        auto         image    = render(device, renderer, graph, outputs, camera);
        for (uint32_t c = 0; c < 3; ++c)
        {
            require(std::abs(image.rgba[center + c] - expected[c]) < 0.003f + std::abs(expected[c]) * 0.002f,
                    "Specular textures must use linear alpha weight and sRGB color");
        }
        gpu.materials[0].normalImage = 0;
        renderer.settings.debugMode  = 2;
        image                        = render(device, renderer, graph, outputs, camera);
        require(std::abs(image.rgba[center] - 0.5f) < 0.003f && std::abs(image.rgba[center + 1] - 0.5f) < 0.003f &&
                    image.rgba[center + 2] > 0.999f,
                "BC5 normal texture lost its positive reconstructed Z component");
        gpu.materials[0].baseColor.a = 0.2f;
        gpu.materials[0].alphaCutoff = 0.5f;
        image                        = render(device, renderer, graph, outputs, camera);
        require(std::abs(image.rgba[center] - 0.02f) < 0.0001f && std::abs(image.rgba[center + 2] - 0.035f) < 0.0001f,
                "Alpha mask did not reveal the background");
        gpu.materials[0].alphaCutoff = -1;
        image                        = render(device, renderer, graph, outputs, camera);
        require(image.rgba[center + 2] > 0.999f && image.rgba[center + 3] == 1,
                "Opaque output must ignore fractional source alpha");
        std::cout << "Material textures: specular channels/sRGB, BC5 normals and opaque/mask coverage passed\n";
    }

    void tangentFramesAndEmission(vultra::Device& device, vultra::Environment& environment)
    {
        for (uint32_t tangentCase : {0u, 1u, 2u})
        {
            vultra::Scene scene;
            // Rotated, mirrored UVs: +U points along world +Y, +V along world +X.
            scene.vertices   = {{{-2, -2, 0}, {0, 0, 1}, {0, 0}},
                                {{2, -2, 0}, {0, 0, 1}, {0, 1}},
                                {{2, 2, 0}, {0, 0, 1}, {1, 1}},
                                {{-2, 2, 0}, {0, 0, 1}, {1, 0}}};
            scene.indices    = {0, 1, 2, 0, 2, 3};
            scene.primitives = {{0, 6, 0}};
            scene.materials.emplace_back();
            scene.radius                     = 3;
            scene.images                     = {{1, 1, {204, 128, 230, 255}}, {1, 1, {64, 128, 192, 255}}};
            scene.materials[0].normalImage   = 0;
            scene.materials[0].emissionImage = 1;
            scene.materials[0].emissionColor = {0.25f, 0.5f, 0.75f};
            if (tangentCase != 0)
            {
                for (auto& vertex : scene.vertices)
                {
                    vertex.tangent = tangentCase == 1 ? glm::vec4(-1, 0, 0, 1) : glm::vec4(0, 0, 1, 1);
                }
            }
            vultra::GpuScene        gpu(device, scene);
            vultra::BuiltinRenderer renderer(device, gpu, environment);
            renderer.settings.ibl              = false;
            renderer.settings.skybox           = false;
            renderer.settings.lightIntensity   = 0;
            renderer.settings.shadowFilter     = vultra::ShadowFilter::eDisabled;
            renderer.settings.shadowResolution = 64;
            const vultra::RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                               glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                               0.1f,
                                               10};
            vultra::RenderGraph        graph(device);
            const auto                 outputs = renderer.addPasses(graph, {65, 65});
            graph.exportResource(outputs.color);
            graph.compile();
            const size_t center         = (32 * 65 + 32) * 4;
            renderer.settings.debugMode = 2;
            for (float scale : {0.0f, 0.5f, 1.0f})
            {
                gpu.materials[0].normalScale = scale;
                const auto      image        = render(device, renderer, graph, outputs, camera);
                const glm::vec3 mapped = (glm::vec3(204, 128, 230) / 255.0f * 2.0f - 1.0f) * glm::vec3(scale, scale, 1);
                auto            world  = glm::vec3(mapped.y, mapped.x, mapped.z);
                if (tangentCase == 1)
                {
                    world = {-mapped.x, -mapped.y, mapped.z};
                }
                else if (tangentCase == 2)
                {
                    // Parallel N/T cannot define a normal-map frame; retain the geometric normal.
                    world = {0, 0, 1};
                }
                const auto expected = glm::normalize(world) * 0.5f + 0.5f;
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    require(std::abs(image.rgba[center + channel] - expected[channel]) < 0.003f,
                            "Normal mapping lost tangent orientation, handedness or normal scale");
                }
            }
            // No light or IBL: the HDR result must be linear emission, without exposure/tone mapping.
            for (uint32_t debug : {0u, 5u})
            {
                renderer.settings.debugMode = debug;
                for (float strength : {0.0f, 3.0f})
                {
                    gpu.materials[0].emissionLuminance = strength;
                    const auto image                   = render(device, renderer, graph, outputs, camera);
                    const auto expected =
                        glm::vec3(0.05126946f, 0.2158605f, 0.5271151f) * scene.materials[0].emissionColor * strength;
                    for (uint32_t channel = 0; channel < 3; ++channel)
                    {
                        require(std::abs(image.rgba[center + channel] - expected[channel]) < 0.002f,
                                "Emission must multiply linearized texture, factor and strength");
                    }
                    auto grazing      = camera;
                    grazing.view      = glm::lookAtRH(glm::vec3(3, 0, 0.2f), glm::vec3(0), glm::vec3(0, 1, 0));
                    const auto angled = render(device, renderer, graph, outputs, grazing);
                    for (uint32_t channel = 0; channel < 3; ++channel)
                    {
                        require(std::abs(angled.rgba[center + channel] - expected[channel]) < 0.002f,
                                "Tangent-space normal perturbation must not extinguish glTF emission");
                    }
                }
            }
        }
        std::cout << "Tangent-frame normals and isolated textured HDR emission passed\n";
    }

    void shadows(vultra::Device& device, vultra::Environment& environment)
    {
        vultra::Scene scene;
        scene.vertices   = {{{-5, 0, -5}, {0, 1, 0}, {0, 0}},
                            {{5, 0, -5}, {0, 1, 0}, {1, 0}},
                            {{5, 0, 5}, {0, 1, 0}, {1, 1}},
                            {{-5, 0, 5}, {0, 1, 0}, {0, 1}},
                            {{-0.7f, 2, -0.7f}, {0, 1, 0}, {0, 0}},
                            {{0.7f, 2, -0.7f}, {0, 1, 0}, {1, 0}},
                            {{0.7f, 2, 0.7f}, {0, 1, 0}, {1, 1}},
                            {{-0.7f, 2, 0.7f}, {0, 1, 0}, {0, 1}}};
        scene.indices    = {0, 2, 1, 0, 3, 2, 4, 6, 5, 4, 7, 6};
        scene.primitives = {{0, 6, 0}, {6, 6, 1}};
        scene.materials.resize(2);
        scene.radius = 8;
        vultra::GpuScene        gpu(device, scene);
        vultra::BuiltinRenderer renderer(device, gpu, environment);
        renderer.settings.skybox           = false;
        renderer.settings.debugMode        = 4;
        renderer.settings.shadowResolution = 512;
        renderer.settings.directionToLight = {-0.6f, 1, 0.1f};
        const vultra::Extent size {256, 256};
        vultra::RenderCamera camera {glm::lookAtRH(glm::vec3(0, 6, 8), glm::vec3(0), glm::vec3(0, 1, 0)),
                                     glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 25.0f),
                                     0.1f,
                                     25};
        const auto           cascades = vultra::calculateCascades(camera,
                                                        renderer.settings.directionToLight,
                                                        scene.center,
                                                        scene.radius,
                                                        512,
                                                        0.7f);
        require(cascades.splits.x > camera.nearPlane && cascades.splits.y > cascades.splits.x &&
                    cascades.splits.z > cascades.splits.y && std::abs(cascades.splits.w - camera.farPlane) < 0.001f,
                "Invalid CSM splits");
        vultra::RenderGraph graph(device);
        const auto          outputs = renderer.addPasses(graph, size);
        graph.exportResource(outputs.color);
        graph.compile();
        auto at = [&](const vultra::Image& image, glm::vec3 world)
        {
            const auto     clip = camera.projection * camera.view * glm::vec4(world, 1);
            const uint32_t x    = uint32_t((clip.x / clip.w * 0.5f + 0.5f) * size.width);
            const uint32_t y    = uint32_t((0.5f - clip.y / clip.w * 0.5f) * size.height);
            return image.rgba.at((size_t(y) * size.width + x) * 4);
        };
        auto softPixels = [](const vultra::Image& image)
        {
            size_t count = 0;
            for (size_t i = 0; i < image.rgba.size(); i += 4)
            {
                if (image.rgba[i] > 0.02f && image.rgba[i] < 0.98f &&
                    std::abs(image.rgba[i] - image.rgba[i + 1]) < 1e-5f)
                {
                    ++count;
                }
            }
            return count;
        };
        renderer.settings.shadowFilter = vultra::ShadowFilter::eDisabled;
        const auto off                 = render(device, renderer, graph, outputs, camera);
        renderer.settings.shadowFilter = vultra::ShadowFilter::eHard;
        const auto hard                = render(device, renderer, graph, outputs, camera);
        require(at(off, {1.2f, 0, -0.2f}) > 0.99f && at(hard, {1.2f, 0, -0.2f}) < 0.01f,
                "CSM did not shadow the analytic receiver point");
        require(at(hard, {3, 0, 2}) > 0.99f, "CSM shadowed an unoccluded point");
        renderer.settings.shadowFilter = vultra::ShadowFilter::ePcf;
        const auto pcf                 = render(device, renderer, graph, outputs, camera);
        require(softPixels(pcf) > softPixels(hard), "PCF must filter shadow boundaries");
        renderer.settings.shadowFilter     = vultra::ShadowFilter::ePcss;
        renderer.settings.sunAngularRadius = 0.003f;
        const auto small                   = render(device, renderer, graph, outputs, camera);
        renderer.settings.sunAngularRadius = 0.1f;
        const auto large                   = render(device, renderer, graph, outputs, camera);
        require(softPixels(large) > softPixels(small), "PCSS penumbra must respond to angular light radius");
        gpu.materials[1].alphaCutoff   = 0.5f;
        gpu.materials[1].baseColor.a   = 0;
        renderer.settings.shadowFilter = vultra::ShadowFilter::eHard;
        const auto masked              = render(device, renderer, graph, outputs, camera);
        require(at(masked, {1.2f, 0, -0.2f}) > 0.99f, "Discarded alpha-mask surface still casts a shadow");
        std::cout << "Shadow soft pixels: hard=" << softPixels(hard) << ", PCF=" << softPixels(pcf)
                  << ", PCSS small=" << softPixels(small) << ", PCSS large=" << softPixels(large) << '\n';
    }
} // namespace

int main()
try
{
    const auto directory = std::filesystem::path("build/.tmp/renderer-tests") /
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(directory);
    const auto    hdr = directory / "constant.hdr";
    std::ofstream file(hdr, std::ios::binary);
    file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
    const unsigned char pixel[4] {128, 64, 32, 129};
    for (uint32_t i = 0; i < 8; ++i)
    {
        file.write(reinterpret_cast<const char*>(pixel), 4);
    }
    file.close();
    vultra::Device      device;
    vultra::Environment environment(device, hdr);
    constantEnvironment(device, environment);
    materialReference(device, environment);
    materialTextures(device, environment, VriFormat_BC5_UNORM);
    materialTextures(device, environment, VriFormat_RG8_UNORM);
    tangentFramesAndEmission(device, environment);
    shadows(device, environment);
    std::cout
        << "Renderer tests passed: constant HDR/IBL mip chain, BRDF LUT, upstream OpenPBR C++ reference, CSM receiver, "
           "PCF and PCSS\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
