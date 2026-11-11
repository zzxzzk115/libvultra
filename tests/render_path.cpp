#include "support/check.hpp"
#include "support/forward_fixture.hpp"

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/render/environment.hpp>
#include <vultra/render/rendering_server.hpp>
#include <vultra/render/scene.hpp>
#include <vultra/render/texture_blit.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <openpbr.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>

namespace
{
    using support::finite;
    using support::ForwardFixture;
    using support::must;
    using support::render;
    using support::require;

    void run(vultra::Frame& frame, vultra::RenderGraph& graph)
    {
        must(graph.execute(must(frame.begin())));
        must(frame.submitAndWait());
    }

    // Begins and ends a cleared 4x4 color pass on the target.
    std::expected<void, vultra::Error>
    clearPass(vultra::Device& device, VriCommandBuffer* cmd, vultra::Texture& target, const float* color)
    {
        const auto view = target.view();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        vultra::beginColorPass(device, cmd, *view, {4, 4}, color);
        device.core.CmdEndRendering(cmd);
        return {};
    }

    void aovMapping(vultra::Device& device)
    {
        using namespace vultra;
        const auto program   = must(ShaderProgram::compile("builtin/shaders/passes/texture_blit.slang",
                                                           {},
                                                           std::array {std::filesystem::path("builtin/shaders")}));
        const auto bindings  = must(program.resourceBindings());
        const auto constants = std::ranges::find(bindings, true, &ShaderResourceBinding::pushConstant);
        require(constants != bindings.end() && constants->uniformSize == 32,
                "Blit target reflection differs from the 32-byte CPU push constants");
        RenderGraph graph(device);
        const auto  input  = must(graph.createTexture("raw", colorTexture({4, 4}, VriFormat_RGBA32_SFLOAT)));
        const auto  output = must(graph.createTexture("mapped", colorTexture({4, 4}, VriFormat_RGBA32_SFLOAT)));
        const float raw[4] {-2, 0, 2, 4};
        must(graph.addPass("Raw AOV",
                           {{input, Usage::eColorWrite}},
                           [&](VriCommandBuffer* cmd, RenderGraph& resources)
                           {
                               return clearPass(device, cmd, resources.getTexture(input), raw);
                           }));
        const auto blit = must(TextureBlit::create(device, VriFormat_RGBA32_SFLOAT));
        ImageView  view {ImageChannel::eRgb, -2, 2};
        must(graph.addPass("Mapped AOV",
                           {{input, Usage::eSampled}, {output, Usage::eColorWrite}},
                           [&](VriCommandBuffer* cmd, RenderGraph& resources) -> std::expected<void, Error>
                           {
                               const float clear[4] {0, 0, 0, 0};
                               if (auto cleared = clearPass(device, cmd, resources.getTexture(output), clear); !cleared)
                               {
                                   return cleared;
                               }
                               return blit->draw(cmd, resources.getTexture(output), {0, 0, 4, 4}, 0, false, view);
                           }));
        must(graph.exportResource(input));
        must(graph.exportResource(output));
        must(graph.compile());
        must(blit->setSource(0, graph.getTexture(input)));
        const auto frame = must(Frame::create(device));
        for (const auto channel : {ImageChannel::eRgb,
                                   ImageChannel::eRed,
                                   ImageChannel::eGreen,
                                   ImageChannel::eBlue,
                                   ImageChannel::eAlpha,
                                   ImageChannel::eLuminance})
        {
            view.channel = channel;
            run(*frame, graph);
            const auto source   = must(readback(device, graph.getTexture(input)));
            const auto expected = must(mapImage(source, view));
            const auto actual   = must(readback(device, graph.getTexture(output)));
            for (size_t i = 0; i < actual.rgba.size(); ++i)
            {
                require(std::abs(actual.rgba[i] - expected.rgba[i]) < 1e-6f, "CPU/GPU AOV mapping differs");
            }
            require(must(imagePixel(source, 1, 2)) == std::array<float, 4> {-2, 0, 2, 4},
                    "Display mapping changed the original float AOV");
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
        constant(must(vultra::readback(device, *environment.radiance)));
        constant(must(vultra::readback(device, *environment.diffuse)));
        for (uint32_t mip : {0u, 4u, 8u})
        {
            constant(must(vultra::readback(device, *environment.specular, mip)));
        }
        const auto lut = must(vultra::readback(device, *environment.brdfLut));
        finite(lut);
        const size_t mirror = (size_t(1) * lut.size.width + lut.size.width - 2) * 4;
        std::cout << "LUT mirror: " << lut.rgba[mirror] << ", " << lut.rgba[mirror + 1] << std::endl;
        require(lut.rgba[mirror] > 0.95f && lut.rgba[mirror + 1] < 0.01f, "BRDF LUT mirror limit");
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

    void renderingServerIds(vultra::Device& device)
    {
        vultra::SceneData scene;
        scene.vertices   = {{{0, 0, 0}, {0, 0, 1}, {0, 0}},
                            {{1, 0, 0}, {0, 0, 1}, {1, 0}},
                            {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
        scene.indices    = {0, 1, 2};
        scene.primitives = {{0, 3, 0}};
        scene.materials.emplace_back();
        vultra::RenderingServer first(device);
        vultra::RenderingServer second(device);
        auto                    firstScene  = must(first.uploadScene(scene));
        auto                    secondScene = must(second.uploadScene(scene));
        const auto              rid         = firstScene.rid();
        require(rid.value != 0 && rid != secondScene.rid(), "GPU scene IDs must be unique");
        require(first.alive(rid) && !second.alive(rid), "GPU scene RID escaped its context");
        support::rejects(second.scene(rid), "Foreign GPU scene RID was accepted");
        const vultra::GpuSceneRid wrongType {(rid.value & ((uint64_t(1) << 56) - 1)) | (uint64_t(2) << 56)};
        require(!first.alive(wrongType) && !first.release(wrongType), "Wrong GPU resource type was accepted");
        support::rejects(first.scene(wrongType), "Wrong GPU resource type was resolved as a scene");
        require(first.release(rid), "First GPU scene release failed");
        require(!first.release(rid) && !first.alive(rid), "Released GPU scene RID remained live");
        support::rejects(firstScene.get(), "Stale GPU scene handle remained usable");
        first.collectCompletedFrame();
    }

    void constantAmbient(vultra::Device& device, vultra::Environment& environment)
    {
        using namespace vultra;
        SceneData scene;
        scene.vertices   = {{{-2, -2, 0}, {0, 0, 1}, {0, 0}},
                            {{2, -2, 0}, {0, 0, 1}, {1, 0}},
                            {{2, 2, 0}, {0, 0, 1}, {1, 1}},
                            {{-2, 2, 0}, {0, 0, 1}, {0, 1}}};
        scene.indices    = {0, 1, 2, 0, 2, 3};
        scene.primitives = {{0, 6, 0}};
        scene.materials.emplace_back();
        scene.materials[0].baseColor              = {0.4f, 0.2f, 0.1f, 1};
        scene.materials[0].baseMetalness          = 1;
        scene.materials[0].occlusionTexture.image = 0;
        scene.images.push_back({1, 1, {64, 64, 64, 255}});
        scene.radius           = 3;
        const auto         gpu = must(GpuScene::create(device, scene));
        const RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                   glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                   0.1f,
                                   10};
        {
            ForwardFixture renderer(device, *gpu, environment);
            renderer.settings.skybox           = false;
            renderer.settings.ibl              = false;
            renderer.settings.directionToLight = {0, 0, -1}; // Backlit surface receives no direct light.
            RenderGraph graph(device);
            const auto  outputs = renderer.addPasses(graph, {65, 65});
            must(graph.exportResource(outputs.hdr));
            must(graph.compile());
            constexpr size_t center = (32 * 65 + 32) * 4;
            const auto       dark   = render(device, renderer, graph, outputs, camera);
            require(dark.rgba[center] < 0.0001f, "Default raster ambient must remain zero");
            renderer.settings.ambientColor = {0.15f, 0.1f, 0.05f};
            const auto      lit            = render(device, renderer, graph, outputs, camera);
            const glm::vec3 expected = glm::vec3(0.15f, 0.1f, 0.05f) * glm::vec3(0.4f, 0.2f, 0.1f) * (64.0f / 255);
            for (size_t channel = 0; channel < 3; ++channel)
            {
                require(std::abs(lit.rgba[center + channel] - expected[channel]) < 0.0001f,
                        "Raster ambient must use linear base color/material AO with IBL disabled");
            }
            renderer.settings.ambientColor = glm::vec3(0);
            const auto reset               = render(device, renderer, graph, outputs, camera);
            require(reset.rgba[center] < 0.0001f, "Ambient edits did not update the existing graph");
        }
        std::cout << "Constant ambient: forward, linear RGB, metal, AO, IBL off and live updates passed\n";
    }

    void emptyGpuScene(vultra::Device& device, vultra::Environment& environment)
    {
        using namespace vultra;
        SceneData scene;
        scene.materials.emplace_back();
        const auto gpu = must(GpuScene::create(device, scene));
        require(gpu->primitives.empty() && gpu->vertices && gpu->indices, "Empty scene GPU buffers are missing");
        ForwardFixture renderer(device, *gpu, environment);
        renderer.settings.skybox = false;
        renderer.settings.ibl    = false;
        RenderGraph graph(device);
        const auto  outputs = renderer.addPasses(graph, {32, 32});
        must(graph.exportResource(outputs.hdr));
        must(graph.compile());
        render(device,
               renderer,
               graph,
               outputs,
               {glm::mat4(1), glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f), 0.1f, 10.0f});
    }

    void livePrimitiveTransform(vultra::Device&              device,
                                vultra::Environment&         environment,
                                const std::filesystem::path& directory)
    {
        using namespace vultra;
        const auto computeFile = directory / "empty_compute.slang";
        std::ofstream(computeFile) << "[shader(\"compute\")] [numthreads(1,1,1)] void main() {}\n";
        VriPipelineLayout*          rawLayout = nullptr;
        const VriPipelineLayoutDesc layoutDesc {};
        must(check(device.core.CreatePipelineLayout(device.handle, &layoutDesc, &rawLayout), "Compute fixture layout"));
        auto destroyLayout = [&device](VriPipelineLayout* handle)
        {
            device.core.DestroyPipelineLayout(handle);
        };
        std::unique_ptr<VriPipelineLayout, decltype(destroyLayout)> computeLayout(rawLayout, destroyLayout);
        const auto                                                  compute = must(ShaderPipeline::create(
            device,
            computeFile,
            {{"main", VriShaderStage_Compute}},
            [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, Error>
            {
                VriPipeline*                 result = nullptr;
                const VriComputePipelineDesc desc {computeLayout.get(), shaders[0], device.pipelineCache};
                if (auto created = check(device.core.CreateComputePipeline(device.handle, &desc, &result),
                                         "Compute fixture pipeline");
                    !created)
                {
                    return std::unexpected(created.error());
                }
                return result;
            }));
        SceneData                                                   scene;
        scene.vertices   = {{{-0.7f, -0.7f, 0}, {0, 0, 1}, {0, 0}},
                            {{0.7f, -0.7f, 0}, {0, 0, 1}, {1, 0}},
                            {{0, 0.7f, 0}, {0, 0, 1}, {0.5f, 1}}};
        scene.indices    = {0, 1, 2};
        scene.primitives = {{0, 3, 0}};
        scene.materials.emplace_back();
        scene.materials[0].emissionColor     = {1, 0, 0};
        scene.materials[0].emissionLuminance = 2;
        scene.radius                         = 1;
        const auto   gpuScene                = must(GpuScene::create(device, scene));
        auto&        gpu                     = *gpuScene;
        RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                             glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                             0.1f,
                             10};
        {
            ForwardFixture renderer(device, gpu, environment);
            renderer.settings.skybox = false;
            renderer.settings.ibl    = false;
            RenderGraph graph(device);
            const auto  outputs = renderer.addPasses(graph, {65, 65});
            must(graph.exportResource(outputs.hdr));
            must(graph.exportResource(outputs.hdr));
            must(graph.compile());
            must(gpu.setPrimitiveTransforms(0, 1, glm::mat4(1)));
            const auto stationary = render(device, renderer, graph, outputs, camera);
            require(renderer.path->visiblePrimitives() == 1, "Visible triangle was culled");
            const auto mixedFrame = must(Frame::create(device));
            for (uint32_t iteration = 0; iteration < 2; ++iteration)
            {
                auto* cmd = must(mixedFrame->begin());
                if (iteration == 0)
                {
                    device.core.CmdSetPipelineLayout(cmd, computeLayout.get());
                    device.core.CmdSetPipeline(cmd, compute->handle());
                    const VriDispatchDesc dispatch {1, 1, 1};
                    device.core.CmdDispatch(cmd, &dispatch);
                }
                renderer.prepare(camera, graph);
                must(graph.execute(cmd));
                // Leave compute bound so the next recording also begins after a compute-only final Pass.
                device.core.CmdSetPipelineLayout(cmd, computeLayout.get());
                device.core.CmdSetPipeline(cmd, compute->handle());
                must(mixedFrame->submitAndWait());
                require(must(readback(device, graph.getTexture(outputs.hdr))).rgba == stationary.rgba,
                        "Compute-to-graphics descriptor binding changed geometry output");
            }
            must(gpu.setPrimitiveTransforms(0,
                                            1,
                                            glm::translate(glm::mat4(1), {1, 0, 0}) *
                                                glm::scale(glm::mat4(1), {-0.5f, 0.8f, 1})));
            const auto partial = render(device, renderer, graph, outputs, camera);
            require(renderer.path->visiblePrimitives() == 1 && std::ranges::any_of(partial.rgba,
                                                                                   [](float value)
                                                                                   {
                                                                                       return value > 1.5f;
                                                                                   }),
                    "A reflected, nonuniformly scaled triangle intersecting the frustum must remain visible");
            must(gpu.setPrimitiveTransforms(0, 1, glm::translate(glm::mat4(1), {2, 0, 0})));
            const auto moved = render(device, renderer, graph, outputs, camera);
            require(renderer.path->visiblePrimitives() == 0, "Offscreen triangle was still submitted");
            const size_t center = (32 * 65 + 32) * 4;
            require(stationary.rgba[center] > 0.5f && moved.rgba[center] < stationary.rgba[center] * 0.05f,
                    "Primitive transform did not move rendered geometry");
            must(gpu.setPrimitiveTransforms(0, 1, glm::scale(glm::mat4(1), {-1, 1, 1})));
            const auto mirrored = render(device, renderer, graph, outputs, camera);
            require(renderer.path->visiblePrimitives() == 1, "Mirrored bounds were incorrectly culled");
            require(mirrored.rgba[center] > 0.5f, "Mirrored primitive was culled");
        }
    }

    // A point light has no main-light fields to travel through: it reaches the shader only as a record of the
    // scene light buffer.
    void sceneLightBuffer(vultra::Device& device, vultra::Environment& environment)
    {
        vultra::SceneData scene;
        scene.vertices   = {{{-2, -2, 0}, {0, 0, 1}, {0, 0}},
                            {{2, -2, 0}, {0, 0, 1}, {1, 0}},
                            {{2, 2, 0}, {0, 0, 1}, {1, 1}},
                            {{-2, 2, 0}, {0, 0, 1}, {0, 1}}};
        scene.indices    = {0, 1, 2, 0, 2, 3};
        scene.primitives = {{0, 6, 0}};
        scene.materials.emplace_back();
        scene.radius        = 3;
        const auto gpuScene = must(vultra::GpuScene::create(device, scene));
        auto&      gpu      = *gpuScene;
        const auto forward  = must(vultra::ForwardPath::create(device));
        auto&      path     = *forward;
        path.settings.ibl   = false;
        const vultra::RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                           glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                           0.1f,
                                           10};
        vultra::RenderGraph        graph(device);
        const auto                 resources = must(path.addPasses(graph, gpu, environment, {65, 65}));
        const auto                 hdr       = must(vultra::findSceneResource(resources, vultra::kSceneHdr));
        must(graph.exportResource(hdr));
        must(graph.compile());
        const auto render = [&](std::span<const vultra::RenderLight> lights)
        {
            must(path.prepare(graph, camera, lights));
            const auto frame = must(vultra::Frame::create(device));
            run(*frame, graph);
            return must(vultra::readback(device, graph.getTexture(hdr)));
        };
        const vultra::RenderLight point {.kind      = vultra::RenderLightKind::ePoint,
                                         .position  = {0, 0, 2},
                                         .color     = {1, 0.5f, 0.25f},
                                         .intensity = 2,
                                         .range     = 4};
        const auto                image = render(std::span(&point, 1));
        // Distance 2 of range 4: inverse-square falloff times the squared smooth range cutoff.
        const float     cutoff      = 1.0f - std::pow(0.5f, 4.0f);
        const float     attenuation = cutoff * cutoff / 4.0f;
        const glm::vec3 expected =
            referenceBrdf(gpu.materials[0], {0, 0, 1}, {0, 0, 1}) * point.color * point.intensity * attenuation;
        const size_t center = (32 * 65 + 32) * 4;
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            require(std::abs(image.rgba[center + channel] - expected[channel]) <=
                        0.003f + std::abs(expected[channel]) * 0.002f,
                    "A point light did not reach the forward shader through the scene light buffer");
        }
        const auto dark = render({});
        require(dark.rgba[center] == 0 && dark.rgba[center + 1] == 0 && dark.rgba[center + 2] == 0,
                "Lights of an earlier frame stayed in the scene light buffer");

        const auto                       lights = must(vultra::LightBuffer::create(device));
        std::vector<vultra::RenderLight> many(vultra::kMaxRenderLights + 1);
        support::rejects(lights->update(many), "A light buffer accepted more lights than it holds");
        const std::array mixed {point, vultra::RenderLight {.directionToLight = {0, 3, 0}}};
        must(lights->update(mixed));
        require(lights->lights().size() == 2 && lights->mainDirectional() == 1 &&
                    lights->lights()[1].directionKind == glm::vec4(0, 1, 0, 0),
                "The light buffer did not record the main directional light with a unit direction");
    }

    void materialReference(vultra::Device& device, vultra::Environment& environment)
    {
        vultra::SceneData scene;
        scene.vertices   = {{{-2, -2, 0}, {0, 0, 1}, {0, 0}},
                            {{2, -2, 0}, {0, 0, 1}, {1, 0}},
                            {{2, 2, 0}, {0, 0, 1}, {1, 1}},
                            {{-2, 2, 0}, {0, 0, 1}, {0, 1}}};
        scene.indices    = {0, 1, 2, 0, 2, 3};
        scene.primitives = {{0, 6, 0}};
        scene.materials.emplace_back();
        scene.radius            = 3;
        const auto     gpuScene = must(vultra::GpuScene::create(device, scene));
        auto&          gpu      = *gpuScene;
        ForwardFixture renderer(device, gpu, environment);
        renderer.settings.ibl              = false;
        renderer.settings.lightIntensity   = 1;
        renderer.settings.lightColor       = {1, 1, 1};
        renderer.settings.directionToLight = {0.4f, 0.6f, 1};
        vultra::RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                     glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                     0.1f,
                                     10};
        vultra::RenderGraph  graph(device);
        const auto           outputs = renderer.addPasses(graph, {65, 65});
        must(graph.exportResource(outputs.hdr));
        must(graph.compile());
        require(graph.activePasses() == std::vector<std::string> {"Skybox", "Forward"},
                "Forward path must add exactly its skybox and forward passes");
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
                    support::fail("Slang OpenPBR differs from the upstream C++ backend");
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
    }

    void materialTextures(vultra::Device& device, vultra::Environment& environment, vultra::TextureFormat normalFormat)
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
        auto& material                      = scene.materials[0];
        material.baseColor                  = {0.6f, 0.2f, 0.1f, 1};
        material.specularWeight             = 0.8f;
        material.specularColor              = {0.9f, 0.7f, 0.5f};
        material.specularTexture.image      = 0;
        material.specularColorTexture.image = 0;
        scene.radius                        = 3;
        asset.textures = must(vultra::prepareTextures(scene, {.compression = vultra::TextureCompression::eNone}));
        // A constant BC5 block with X=Y=128/255; Z must be reconstructed by the shader.
        std::vector<std::byte> block(16);
        block[0] = block[1] = block[8] = block[9] = std::byte(128);
        const auto normalSlot                     = uint32_t(asset.textures.images.size());
        if (normalFormat == vultra::TextureFormat::eRg8Unorm)
        {
            block.assign(4 * 4 * 2, std::byte(128));
        }
        asset.textures.images.push_back({normalFormat, {{{4, 4}, std::move(block)}}});
        asset.textures.materials[0][2] = normalSlot;
        const auto     gpuScene        = must(vultra::GpuScene::create(device, asset));
        auto&          gpu             = *gpuScene;
        ForwardFixture renderer(device, gpu, environment);
        renderer.settings.ibl              = false;
        renderer.settings.skybox           = false;
        renderer.settings.lightIntensity   = 1;
        renderer.settings.lightColor       = {1, 1, 1};
        renderer.settings.directionToLight = {0.4f, 0.6f, 1};
        const vultra::RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                           glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                           0.1f,
                                           10};
        vultra::RenderGraph        graph(device);
        const auto                 outputs = renderer.addPasses(graph, {65, 65});
        must(graph.exportResource(outputs.hdr));
        must(graph.compile());
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
        gpu.materials[0].normalTexture.image = 0;
        renderer.settings.debugMode          = 2;
        image                                = render(device, renderer, graph, outputs, camera);
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
            vultra::SceneData scene;
            // Rotated, mirrored UVs: +U points along world +Y, +V along world +X.
            scene.vertices   = {{{-2, -2, 0}, {0, 0, 1}, {0, 0}},
                                {{2, -2, 0}, {0, 0, 1}, {0, 1}},
                                {{2, 2, 0}, {0, 0, 1}, {1, 1}},
                                {{-2, 2, 0}, {0, 0, 1}, {1, 0}}};
            scene.indices    = {0, 1, 2, 0, 2, 3};
            scene.primitives = {{0, 6, 0}};
            scene.materials.emplace_back();
            scene.radius                             = 3;
            scene.images                             = {{1, 1, {204, 128, 230, 255}}, {1, 1, {64, 128, 192, 255}}};
            scene.materials[0].normalTexture.image   = 0;
            scene.materials[0].emissionTexture.image = 1;
            scene.materials[0].emissionColor         = {0.25f, 0.5f, 0.75f};
            if (tangentCase != 0)
            {
                for (auto& vertex : scene.vertices)
                {
                    vertex.tangent = tangentCase == 1 ? glm::vec4(-1, 0, 0, 1) : glm::vec4(0, 0, 1, 1);
                }
            }
            const auto     gpuScene = must(vultra::GpuScene::create(device, scene));
            auto&          gpu      = *gpuScene;
            ForwardFixture renderer(device, gpu, environment);
            renderer.settings.ibl            = false;
            renderer.settings.skybox         = false;
            renderer.settings.lightIntensity = 0;
            const vultra::RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                               glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                               0.1f,
                                               10};
            vultra::RenderGraph        graph(device);
            const auto                 outputs = renderer.addPasses(graph, {65, 65});
            must(graph.exportResource(outputs.hdr));
            must(graph.compile());
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

    // A second, unrelated render path written only against the public interfaces: it shades world normals
    // with its own shader and layout. Later passes consume it through the same standard resource names.
    class NormalPath final : public vultra::RenderPath
    {
    public:
        NormalPath(vultra::Device& device, const std::filesystem::path& directory) :
            m_Device(device)
        {
            const auto file = directory / "normal_path.slang";
            std::ofstream(file) << R"(
struct Parameters { float4x4 clipFromLocal; float4x4 worldFromLocal; };
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
struct VertexOutput { float4 position : SV_Position; float3 normal : NORMAL; };
[shader("vertex")] VertexOutput vertexMain(float3 position : POSITION, float3 normal : NORMAL)
{
    VertexOutput output;
    output.position = mul(parameters.clipFromLocal, float4(position, 1));
    output.normal   = mul(parameters.worldFromLocal, float4(normal, 0)).xyz;
    return output;
}
[shader("fragment")] float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(normalize(input.normal) * 0.5 + 0.5, 1);
}
)";
            VriPushConstantDesc   push {0, sizeof(Parameters), VriShaderStage_Vertex};
            VriPipelineLayoutDesc layout {};
            layout.pushConstants   = &push;
            layout.pushConstantNum = 1;
            layout.shaderStages    = push.shaderStages;
            must(vultra::check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout),
                               "Normal path layout"));
            m_Pipeline = must(vultra::ShaderPipeline::create(
                device,
                file,
                std::vector<vultra::ShaderEntry> {{"vertexMain", VriShaderStage_Vertex},
                                                  {"fragmentMain", VriShaderStage_Fragment}},
                [this](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, vultra::Error>
                {
                    VriVertexStreamDesc    stream {sizeof(vultra::SceneVertex), 0, VriVertexStepRate_PerVertex};
                    VriVertexAttributeDesc attributes[2] {};
                    attributes[0].format = VriFormat_RGB32_SFLOAT;
                    attributes[0].offset = offsetof(vultra::SceneVertex, position);
                    attributes[1].format = VriFormat_RGB32_SFLOAT;
                    attributes[1].offset = offsetof(vultra::SceneVertex, normal);
                    VriColorAttachmentDesc color {};
                    color.format         = VriFormat_RGBA16_SFLOAT;
                    color.colorWriteMask = VriColorWrite_RGBA;
                    VriGraphicsPipelineDesc desc {};
                    desc.pipelineCache                   = m_Device.pipelineCache;
                    desc.pipelineLayout                  = m_Layout;
                    desc.shaders                         = shaders.data();
                    desc.shaderNum                       = uint32_t(shaders.size());
                    desc.vertexInput                     = {attributes, 2, &stream, 1};
                    desc.inputAssembly.topology          = VriPrimitiveTopology_TriangleList;
                    desc.rasterization.cullMode          = VriCullMode_None;
                    desc.rasterization.lineWidth         = 1;
                    desc.multisample.sampleNum           = 1;
                    desc.outputMerger.colors             = &color;
                    desc.outputMerger.colorNum           = 1;
                    desc.depthStencil.depthTest          = VRI_TRUE;
                    desc.depthStencil.depthWrite         = VRI_TRUE;
                    desc.depthStencil.depthCompareOp     = VriCompareOp_Less;
                    desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
                    VriPipeline* result                  = nullptr;
                    if (auto created =
                            vultra::check(m_Device.core.CreateGraphicsPipeline(m_Device.handle, &desc, &result),
                                          "Normal path pipeline");
                        !created)
                    {
                        return std::unexpected(created.error());
                    }
                    return result;
                }));
        }

        ~NormalPath() override
        {
            m_Device.waitIdleForDestruction();
            m_Pipeline.reset();
            m_Device.core.DestroyPipelineLayout(m_Layout);
        }

        std::span<const vultra::SceneResourceDesc> provides() const override
        {
            return m_Provided;
        }

        std::expected<vultra::SceneResources, vultra::Error> addPasses(vultra::RenderGraph& graph,
                                                                       vultra::GpuScene&    scene,
                                                                       vultra::Environment&,
                                                                       vultra::Extent size) override
        {
            using namespace vultra;
            m_Scene          = &scene;
            const auto color = graph.createTexture(std::string(kSceneHdr), colorTexture(size, VriFormat_RGBA16_SFLOAT));
            if (!color)
            {
                return std::unexpected(color.error());
            }
            auto depthDesc = depthTexture(size);
            depthDesc.usage |= VriTextureUsage_TransferSrc;
            const auto depth = graph.createTexture(std::string(kSceneDepth), depthDesc);
            if (!depth)
            {
                return std::unexpected(depth.error());
            }
            auto added = graph.addPass(
                "Normals",
                {{*color, Usage::eColorWrite, true}, {*depth, Usage::eDepthWrite, true}},
                [this, color = *color, depth = *depth, size](VriCommandBuffer* cmd,
                                                             RenderGraph&      resources) -> std::expected<void, Error>
                {
                    const auto colorView = resources.getTexture(color).view();
                    if (!colorView)
                    {
                        return std::unexpected(colorView.error());
                    }
                    const auto depthView = resources.getTexture(depth).view();
                    if (!depthView)
                    {
                        return std::unexpected(depthView.error());
                    }
                    VriAttachmentDesc colorAttachment {};
                    colorAttachment.view    = *colorView;
                    colorAttachment.loadOp  = VriAttachmentLoadOp_Clear;
                    colorAttachment.storeOp = VriAttachmentStoreOp_Store;
                    VriAttachmentDesc depthAttachment {};
                    depthAttachment.view                          = *depthView;
                    depthAttachment.loadOp                        = VriAttachmentLoadOp_Clear;
                    depthAttachment.storeOp                       = VriAttachmentStoreOp_Store;
                    depthAttachment.clearValue.depthStencil.depth = 1;
                    VriAttachmentsDesc attachments {};
                    attachments.colors     = &colorAttachment;
                    attachments.colorNum   = 1;
                    attachments.depth      = &depthAttachment;
                    attachments.renderArea = {0, 0, size.width, size.height};
                    attachments.layerNum   = 1;
                    m_Device.core.CmdBeginRendering(cmd, &attachments);
                    const VriViewport viewport {0, 0, float(size.width), float(size.height), 0, 1};
                    const VriRect     scissor {0, 0, size.width, size.height};
                    m_Device.core.CmdSetViewports(cmd, &viewport, 1);
                    m_Device.core.CmdSetScissors(cmd, &scissor, 1);
                    m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
                    m_Device.core.CmdSetPipeline(cmd, m_Pipeline->handle());
                    const VriVertexBufferBinding vertices {m_Scene->vertices->handle, 0};
                    m_Device.core.CmdSetVertexBuffers(cmd, 0, &vertices, 1);
                    m_Device.core.CmdSetIndexBuffer(cmd, m_Scene->indices->handle, 0, VriIndexType_UInt32);
                    for (uint32_t index = 0; index < m_Scene->primitives.size(); ++index)
                    {
                        const auto&      primitive = m_Scene->primitives[index];
                        const auto&      world     = m_Scene->primitiveTransform(index);
                        const Parameters parameters {m_ViewProjection * world, world};
                        m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
                        const VriDrawIndexedDesc draw {primitive.indexCount, 1, primitive.firstIndex, 0, 0};
                        m_Device.core.CmdDrawIndexed(cmd, &draw);
                    }
                    m_Device.core.CmdEndRendering(cmd);
                    return {};
                });
            if (!added)
            {
                return std::unexpected(added.error());
            }
            return SceneResources {{std::string(kSceneHdr), *color}, {std::string(kSceneDepth), *depth}};
        }

        std::expected<void, vultra::Error>
        prepare(vultra::RenderGraph&, const vultra::RenderCamera& camera, std::span<const vultra::RenderLight>) override
        {
            m_ViewProjection = camera.projection * camera.view;
            return {};
        }

    private:
        struct Parameters
        {
            glm::mat4 clipFromLocal;
            glm::mat4 worldFromLocal;
        };

        vultra::Device&                          m_Device;
        vultra::GpuScene*                        m_Scene  = nullptr;
        VriPipelineLayout*                       m_Layout = nullptr;
        std::unique_ptr<vultra::ShaderPipeline>  m_Pipeline;
        glm::mat4                                m_ViewProjection {1};
        std::array<vultra::SceneResourceDesc, 2> m_Provided {
            vultra::SceneResourceDesc {std::string(vultra::kSceneHdr), VriFormat_RGBA16_SFLOAT, "color.linear.hdr"},
            vultra::SceneResourceDesc {std::string(vultra::kSceneDepth), VriFormat_D32_SFLOAT, "depth"}};
    };

    // The same scene, camera and consumer code run through two render paths selected at runtime.
    void
    switchRenderPaths(vultra::Device& device, vultra::Environment& environment, const std::filesystem::path& directory)
    {
        using namespace vultra;
        SceneData scene;
        scene.vertices   = {{{-2, -2, 0}, {0, 0, 1}, {0, 0}},
                            {{2, -2, 0}, {0, 0, 1}, {1, 0}},
                            {{2, 2, 0}, {0, 0, 1}, {1, 1}},
                            {{-2, 2, 0}, {0, 0, 1}, {0, 1}}};
        scene.indices    = {0, 1, 2, 0, 2, 3};
        scene.primitives = {{0, 6, 0}};
        scene.materials.emplace_back();
        scene.materials[0].emissionColor     = {0, 0, 1};
        scene.materials[0].emissionLuminance = 3;
        scene.radius                         = 3;
        const auto         gpu               = must(GpuScene::create(device, scene));
        const RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                   glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                   0.1f,
                                   10};
        const auto         forwardPath = must(ForwardPath::create(device));
        auto&              forward     = *forwardPath;
        NormalPath         normals(device, directory);
        forward.settings.ibl                       = false;
        forward.settings.skybox                    = false;
        constexpr size_t                    center = (32 * 65 + 32) * 4;
        std::array<std::array<float, 3>, 2> colors {};
        std::array<float, 2>                depths {};
        const std::array<RenderPath*, 2>    paths {&forward, &normals};
        for (size_t index = 0; index < paths.size(); ++index)
        {
            RenderGraph graph(device);
            const auto  resources = must(paths[index]->addPasses(graph, *gpu, environment, {65, 65}));
            must(validateSceneResources(*paths[index], resources, graph));
            const auto hdr   = must(findSceneResource(resources, kSceneHdr));
            const auto depth = must(findSceneResource(resources, kSceneDepth));
            must(graph.exportResource(hdr));
            must(graph.exportResource(depth));
            must(graph.compile());
            must(paths[index]->prepare(graph, camera, {}));
            const auto frame = must(Frame::create(device));
            run(*frame, graph);
            const auto color = must(readback(device, graph.getTexture(hdr)));
            finite(color);
            colors[index] = {color.rgba[center], color.rgba[center + 1], color.rgba[center + 2]};
            depths[index] = must(readback(device, graph.getTexture(depth))).rgba[center];
        }
        require(colors[0][2] > 2.9f && colors[0][0] < 0.01f, "Forward path did not shade the emissive quad");
        require(std::abs(colors[1][0] - 0.5f) < 0.01f && std::abs(colors[1][1] - 0.5f) < 0.01f && colors[1][2] > 0.99f,
                "Normal path did not write the +Z world normal");
        require(depths[0] > 0 && depths[0] < 1 && std::abs(depths[0] - depths[1]) < 1e-6f,
                "Two render paths disagree on scene.depth for the same geometry and camera");

        struct Incomplete final : RenderPath
        {
            std::span<const SceneResourceDesc> provides() const override
            {
                return declared;
            }

            std::expected<SceneResources, Error> addPasses(RenderGraph&, GpuScene&, Environment&, Extent) override
            {
                return SceneResources {};
            }

            std::expected<void, Error> prepare(RenderGraph&, const RenderCamera&, std::span<const RenderLight>) override
            {
                return {};
            }

            std::array<SceneResourceDesc, 1> declared {
                SceneResourceDesc {std::string(kSceneHdr), VriFormat_RGBA16_SFLOAT, "color.linear.hdr"}};
        } incomplete;

        RenderGraph empty(device);
        support::rejects(validateSceneResources(incomplete, {}, empty),
                         "A render path without scene.depth passed validation");
        std::cout << "Render paths: forward and a custom normal path share scene.hdr/scene.depth; contract checked\n";
    }
} // namespace

int main()
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
    const auto devicePtr = must(vultra::Device::create());
    auto&      device    = *devicePtr;
    renderingServerIds(device);
    aovMapping(device);
    const auto environmentPtr = must(vultra::Environment::create(device, hdr));
    auto&      environment    = *environmentPtr;
    constantEnvironment(device, environment);
    constantAmbient(device, environment);
    emptyGpuScene(device, environment);
    livePrimitiveTransform(device, environment, directory);
    sceneLightBuffer(device, environment);
    materialReference(device, environment);
    materialTextures(device, environment, vultra::TextureFormat::eBc5Unorm);
    materialTextures(device, environment, vultra::TextureFormat::eRg8Unorm);
    tangentFramesAndEmission(device, environment);
    switchRenderPaths(device, environment, directory);
    std::cout << "Forward path tests passed: constant HDR/IBL mip chain, BRDF LUT, upstream OpenPBR C++ reference, "
                 "material textures and live transforms\n";
    return 0;
}
