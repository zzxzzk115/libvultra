#include "support/check.hpp"
#include "support/cleared_texture.hpp"
#include "support/forward_fixture.hpp"

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/render/environment.hpp>
#include <vultra/render/rendering_server.hpp>
#include <vultra/render/scene.hpp>
#include <vultra/render/texture_blit.hpp>
#include <vultra/render/tone_mapping_pass.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <openpbr.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>

namespace
{
    using support::clearedTexture;
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

    // The value this device samples from a constant texture. Hardware sRGB decoding may differ slightly from the
    // exact curve: an Intel Raptor Lake GPU returns about 0.2% more.
    glm::vec3 sampledTexel(vultra::Device& device, vultra::Texture& source)
    {
        using namespace vultra;
        auto       target = must(Texture::create(device, colorTexture({1, 1}, VriFormat_RGBA32_SFLOAT)));
        const auto blit   = must(TextureBlit::create(device, target.desc.format));
        const auto frame  = must(Frame::create(device));
        must(blit->setSource(0, source));
        auto* cmd = must(frame->begin());
        must(blit->draw(cmd, target, {0, 0, 1, 1}));
        must(frame->submitAndWait());
        const auto image = must(readback(device, target));
        return {image.rgba[0], image.rgba[1], image.rgba[2]};
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
        RenderGraph                graph(device);
        const std::array<float, 4> raw {-2, 0, 2, 4};
        const auto input  = must(graph.createTexture("raw", clearedTexture({4, 4}, raw, VriFormat_RGBA32_SFLOAT)));
        const auto output = must(graph.createTexture("mapped", colorTexture({4, 4}, VriFormat_RGBA32_SFLOAT)));
        must(graph.addPass("Raw AOV",
                           {{input, Usage::eColorWrite}},
                           [&](VriCommandBuffer* cmd, RenderGraph& resources)
                           {
                               return clearPass(device, cmd, resources.getTexture(input), raw.data());
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
            require(renderer.shading->visiblePrimitives() == 1, "Visible triangle was culled");
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
            require(renderer.shading->visiblePrimitives() == 1 && std::ranges::any_of(partial.rgba,
                                                                                      [](float value)
                                                                                      {
                                                                                          return value > 1.5f;
                                                                                      }),
                    "A reflected, nonuniformly scaled triangle intersecting the frustum must remain visible");
            must(gpu.setPrimitiveTransforms(0, 1, glm::translate(glm::mat4(1), {2, 0, 0})));
            const auto moved = render(device, renderer, graph, outputs, camera);
            require(renderer.shading->visiblePrimitives() == 0, "Offscreen triangle was still submitted");
            const size_t center = (32 * 65 + 32) * 4;
            require(stationary.rgba[center] > 0.5f && moved.rgba[center] < stationary.rgba[center] * 0.05f,
                    "Primitive transform did not move rendered geometry");
            must(gpu.setPrimitiveTransforms(0, 1, glm::scale(glm::mat4(1), {-1, 1, 1})));
            const auto mirrored = render(device, renderer, graph, outputs, camera);
            require(renderer.shading->visiblePrimitives() == 1, "Mirrored bounds were incorrectly culled");
            require(mirrored.rgba[center] > 0.5f, "Mirrored primitive was culled");
        }
    }

    // The G-buffer layers forward shading writes on request: a quad facing the camera, moved and then looked at
    // from elsewhere, read back against the normal and the motion computed on the CPU. scene.hdr stays what
    // the path renders without G-buffer layers.
    void gbufferLayers(vultra::Device& device, vultra::Environment& environment)
    {
        using namespace vultra;
        SceneData scene;
        scene.vertices   = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}},
                            {{1, -1, 0}, {0, 0, 1}, {1, 0}},
                            {{1, 1, 0}, {0, 0, 1}, {1, 1}},
                            {{-1, 1, 0}, {0, 0, 1}, {0, 1}}};
        scene.indices    = {0, 1, 2, 0, 2, 3};
        scene.primitives = {{0, 6, 0}};
        scene.materials.emplace_back();
        scene.materials[0].emissionColor     = {0.2f, 0.4f, 0.6f};
        scene.materials[0].emissionLuminance = 1;
        scene.radius                         = 2;
        const auto         gpu               = must(GpuScene::create(device, scene));
        constexpr uint32_t kSize             = 64;
        const RenderView   view {"main", {kSize, kSize}};
        const glm::mat4    projection = glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f);
        const auto         cameraAt   = [&](glm::vec3 eye)
        {
            return RenderCamera {glm::lookAtRH(eye, eye - glm::vec3(0, 0, 1), glm::vec3(0, 1, 0)),
                                 projection,
                                 0.1f,
                                 10};
        };

        struct Rendered
        {
            Image hdr;
            Image normal;
            Image motion;
        };

        const auto renderWith = [&](std::span<const std::string> requested, std::span<const RenderCamera> cameras)
        {
            auto  path               = must(createForwardPath(device));
            auto* shading            = static_cast<ForwardShadingStage*>(path->stage("shading"));
            shading->settings.skybox = false;
            shading->settings.ibl    = false;
            RenderGraph graph(device);
            const auto  resources =
                must(path->addPasses(graph, *gpu, environment, std::span(&view, 1), requested)).front();
            const auto hdr = must(findSceneResource(resources, kSceneHdr));
            must(graph.exportResource(hdr));
            std::optional<RenderGraph::Resource> normal;
            std::optional<RenderGraph::Resource> motion;
            if (!requested.empty())
            {
                normal = must(findSceneResource(resources, kSceneNormal));
                motion = must(findSceneResource(resources, kSceneMotion));
                must(graph.exportResource(*normal));
                must(graph.exportResource(*motion));
            }
            else
            {
                require(!findSceneResource(resources, kSceneNormal), "Forward wrote G-buffer layers no one requested");
            }
            must(graph.compile());
            std::vector<Rendered> frames;
            for (size_t index = 0; index < cameras.size(); ++index)
            {
                must(gpu->beginFrame());
                // Frame 1 moves the quad; frame 2 keeps it and moves the camera.
                must(gpu->setPrimitiveTransforms(0,
                                                 1,
                                                 index == 0 ? glm::mat4(1) :
                                                              glm::translate(glm::mat4(1), {0.25f, 0, 0})));
                must(path->prepare(graph, cameras.subspan(index, 1), {}));
                const auto frame = must(Frame::create(device));
                auto*      cmd   = must(frame->begin());
                must(graph.execute(cmd));
                must(frame->submitAndWait());
                Rendered rendered {must(readback(device, graph.getTexture(hdr)))};
                if (normal)
                {
                    rendered.normal = must(readback(device, graph.getTexture(*normal)));
                    rendered.motion = must(readback(device, graph.getTexture(*motion)));
                }
                frames.push_back(std::move(rendered));
            }
            return frames;
        };
        const std::array                 cameras {cameraAt({0, 0, 3}), cameraAt({0, 0, 3}), cameraAt({0.2f, 0.1f, 3})};
        const std::array<std::string, 2> requested {std::string(kSceneNormal), std::string(kSceneMotion)};
        const auto                       layered = renderWith(requested, cameras);
        const auto                       plain   = renderWith({}, cameras);
        // Where the quad at z = 0 is seen through a pixel centre, and where that point was in the previous frame.
        const auto expectedMotion =
            [&](const glm::mat4& current, const glm::mat4& previous, glm::vec3 offset, glm::vec2 pixel)
        {
            const glm::vec2 uv = (pixel + 0.5f) / float(kSize);
            const glm::vec2 ndc {uv.x * 2 - 1, 1 - uv.y * 2};
            const auto      inverse   = glm::inverse(current);
            glm::vec4       nearPoint = inverse * glm::vec4(ndc, 0, 1);
            glm::vec4       farPoint  = inverse * glm::vec4(ndc, 1, 1);
            nearPoint /= nearPoint.w;
            farPoint /= farPoint.w;
            const float     t     = nearPoint.z / (nearPoint.z - farPoint.z);
            const glm::vec3 point = glm::vec3(nearPoint) + t * glm::vec3(farPoint - nearPoint);
            const glm::vec4 clip  = previous * glm::vec4(point - offset, 1);
            return (glm::vec2(clip) / clip.w - ndc) * glm::vec2(0.5f, -0.5f);
        };
        const std::array<glm::mat4, 3> viewProjections {projection * cameras[0].view,
                                                        projection * cameras[1].view,
                                                        projection * cameras[2].view};
        const std::array<glm::vec3, 3> offsets {glm::vec3(0), glm::vec3(0.25f, 0, 0), glm::vec3(0)};
        float                          largest = 0;
        for (size_t index = 0; index < cameras.size(); ++index)
        {
            require(layered[index].hdr.rgba == plain[index].hdr.rgba, "G-buffer layers changed scene.hdr");
            const auto& previous = viewProjections[index == 0 ? 0 : index - 1];
            for (const glm::vec2 pixel : {glm::vec2(32, 32), glm::vec2(20, 40), glm::vec2(44, 24)})
            {
                const size_t texel = (size_t(pixel.y) * kSize + size_t(pixel.x)) * 4;
                // +Z is the centre of the octahedral encoding.
                const glm::vec2 normal {layered[index].normal.rgba[texel], layered[index].normal.rgba[texel + 1]};
                require(glm::all(glm::epsilonEqual(normal, glm::vec2(0, 0), 1e-3f)), "scene.normal is not +Z");
                const glm::vec2 motion {layered[index].motion.rgba[texel], layered[index].motion.rgba[texel + 1]};
                const auto      expected = expectedMotion(viewProjections[index], previous, offsets[index], pixel);
                largest                  = std::max(largest, glm::length(expected));
                if (!glm::all(glm::epsilonEqual(motion, expected, 2e-3f)))
                {
                    std::cerr << "frame " << index << " pixel " << pixel.x << "," << pixel.y << ": motion " << motion.x
                              << "," << motion.y << " expected " << expected.x << "," << expected.y << "\n";
                    require(false, "scene.motion differs from the analytic motion");
                }
            }
        }
        require(largest > 0.03f, "The moves were too small to test scene.motion");
        std::cout << "G-buffer layers: scene.normal and scene.motion (object and camera, largest " << largest
                  << " UV) match analytic values; scene.hdr equals the path without layers\n";
    }

    // The display slot runs after the caller's own passes: final from vultra.tone_mapping equals tone mapping
    // scene.hdr directly, and a stage parameter set through the path changes it.
    void displayStage(vultra::Device& device, vultra::Environment& environment)
    {
        using namespace vultra;
        SceneData scene;
        scene.vertices   = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}},
                            {{1, -1, 0}, {0, 0, 1}, {1, 0}},
                            {{0, 1, 0}, {0, 0, 1}, {0.5f, 1}}};
        scene.indices    = {0, 1, 2};
        scene.primitives = {{0, 3, 0}};
        scene.materials.emplace_back();
        scene.materials[0].emissionColor     = {0.5f, 0.3f, 0.1f};
        scene.materials[0].emissionLuminance = 1;
        scene.radius                         = 1;
        const auto         gpu               = must(GpuScene::create(device, scene));
        auto               path              = must(createForwardPath(device));
        const RenderView   view {"main", {48, 48}};
        const RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                   glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                   0.1f,
                                   10};
        RenderGraph        graph(device);
        const auto         resources = must(path->addPasses(graph, *gpu, environment, std::span(&view, 1))).front();
        const auto         output    = must(findSceneResource(resources, kSceneOutput));
        require(output.index == must(findSceneResource(resources, kSceneHdr)).index, "scene.output is not scene.hdr");
        const auto final     = must(path->addDisplay(graph, std::span(&output, 1), VriFormat_RGBA8_UNORM)).front();
        auto       reference = must(ToneMappingPass::create(device));
        const std::array<double, 3> defaults {0, 0, 0};
        const auto direct = must(reference->addPasses(graph, "Reference", std::span(&output, 1), defaults)).front();
        must(graph.exportResource(final));
        must(graph.exportResource(direct));
        must(graph.compile());
        const auto renderFrame = [&]
        {
            must(path->prepare(graph, std::span(&camera, 1), {}));
            const auto frame = must(Frame::create(device));
            auto*      cmd   = must(frame->begin());
            must(graph.execute(cmd));
            must(frame->submitAndWait());
            return std::pair {must(readback(device, graph.getTexture(final))),
                              must(readback(device, graph.getTexture(direct)))};
        };
        const auto [shown, expected] = renderFrame();
        require(shown.rgba == expected.rgba, "The display stage differs from tone mapping scene.hdr");
        require(!path->setParameter("display", "exposure", 99), "An exposure outside its range was accepted");
        must(path->setParameter("display", "exposure", 2));
        require(must(path->parameter("display", "exposure")) == 2, "The exposure was not kept");
        const auto [brighter, unchanged] = renderFrame();
        const size_t center              = (24 * 48 + 24) * 4;
        require(brighter.rgba[center] > shown.rgba[center] + 0.05f && unchanged.rgba == expected.rgba,
                "The display exposure parameter did not change final alone");
        std::cout << "Display slot: final equals tone mapping scene.output; display.exposure changes it\n";
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
        const auto forward  = must(vultra::createForwardPath(device));
        auto&      path     = *forward;
        static_cast<vultra::ForwardShadingStage*>(path.stage("shading"))->settings.ibl = false;
        const vultra::RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                                           glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                           0.1f,
                                           10};
        vultra::RenderGraph        graph(device);
        const vultra::RenderView   view {"main", {65, 65}};
        const auto resources = must(path.addPasses(graph, gpu, environment, std::span(&view, 1))).front();
        const auto hdr       = must(vultra::findSceneResource(resources, vultra::kSceneHdr));
        must(graph.exportResource(hdr));
        must(graph.compile());
        const auto render = [&](std::span<const vultra::RenderLight> lights)
        {
            must(path.prepare(graph, std::span(&camera, 1), lights));
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
            // The emission texel (64, 128, 192) is sampled as sRGB, within this device's decoding precision.
            const auto texel = sampledTexel(device, *gpu.materialTextures.at(0)[4]);
            const auto exact = glm::vec3(0.05126946f, 0.2158605f, 0.5271151f);
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                require(std::abs(texel[channel] - exact[channel]) < 0.0015f,
                        "The emission texture is not decoded from sRGB");
            }
            // No light or IBL: the HDR result must be linear emission, without exposure/tone mapping.
            for (uint32_t debug : {0u, 5u})
            {
                renderer.settings.debugMode = debug;
                for (float strength : {0.0f, 3.0f})
                {
                    gpu.materials[0].emissionLuminance = strength;
                    const auto image                   = render(device, renderer, graph, outputs, camera);
                    const auto expected                = texel * scene.materials[0].emissionColor * strength;
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

    // A second, unrelated shading stage written only against the public interfaces: it shades world normals
    // with its own shader and layout. When a frame stage provides test.background, each view starts from a
    // copy of it. Later passes consume it through the same standard resource names.
    class NormalStage final : public vultra::RenderStage
    {
    public:
        static vultra::StageType type(const std::filesystem::path& directory)
        {
            using namespace vultra;
            return {
                {"test.normal_shading",
                 {"shading"},
                 StageScope::eView,
                 {{std::string(kSceneVertices)}, {std::string(kSceneIndices)}, {"test.background", true}},
                 {{std::string(kSceneHdr), VriFormat_RGBA16_SFLOAT, "color.linear.hdr"},
                  {std::string(kSceneDepth), VriFormat_D32_SFLOAT, "depth"}}},
                [directory](Device& device, const AssetSource*) -> std::expected<std::unique_ptr<RenderStage>, Error>
                {
                    return std::make_unique<NormalStage>(device, directory);
                }};
        }

        NormalStage(vultra::Device& device, const std::filesystem::path& directory) :
            m_Device(device)
        {
            const auto file = directory / "normal_stage.slang";
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
                               "Normal stage layout"));
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
                                          "Normal stage pipeline");
                        !created)
                    {
                        return std::unexpected(created.error());
                    }
                    return result;
                }));
        }

        ~NormalStage() override
        {
            m_Device.waitIdleForDestruction();
            m_Pipeline.reset();
            m_Device.core.DestroyPipelineLayout(m_Layout);
        }

        std::expected<vultra::SceneResources, vultra::Error> addPasses(vultra::StageBuild& build) override
        {
            using namespace vultra;
            m_Scene = &build.scene;
            m_ViewProjections.resize(build.views.size());
            const auto size     = build.views[*build.view].size;
            const auto vertices = findSceneResource(build.resources, kSceneVertices);
            const auto indices  = findSceneResource(build.resources, kSceneIndices);
            if (!vertices || !indices)
            {
                return makeError("The normal stage needs the geometry buffers");
            }
            const auto color =
                build.graph.createTexture(build.graphName(kSceneHdr), colorTexture(size, VriFormat_RGBA16_SFLOAT));
            if (!color)
            {
                return std::unexpected(color.error());
            }
            auto depthDesc = depthTexture(size);
            depthDesc.usage |= VriTextureUsage_TransferSrc;
            const auto depth = build.graph.createTexture(build.graphName(kSceneDepth), depthDesc);
            if (!depth)
            {
                return std::unexpected(depth.error());
            }
            const auto background = findSceneResource(build.resources, "test.background");
            if (background)
            {
                auto copied = build.graph.addPass(
                    build.graphName("Copy background"),
                    {{*background, Usage::eCopySource}, {*color, Usage::eCopyDestination, true}},
                    [this, source = *background, target = *color](VriCommandBuffer* cmd,
                                                                  RenderGraph& resources) -> std::expected<void, Error>
                    {
                        VriTextureCopyDesc copy {};
                        copy.src.layerNum = 1;
                        copy.dst.layerNum = 1;
                        copy.src.aspect   = VriImageAspect_Color;
                        copy.dst.aspect   = VriImageAspect_Color;
                        m_Device.core.CmdCopyTexture(cmd,
                                                     resources.getTexture(target).handle,
                                                     resources.getTexture(source).handle,
                                                     &copy);
                        return {};
                    });
                if (!copied)
                {
                    return std::unexpected(copied.error());
                }
            }
            const bool keep  = bool(background);
            auto       added = build.graph.addPass(
                build.graphName("Normals"),
                {{*color, keep ? Usage::eColorReadWrite : Usage::eColorWrite, !keep},
                       {*depth, Usage::eDepthWrite, true},
                       {*vertices, Usage::eVertexBuffer},
                       {*indices, Usage::eIndexBuffer}},
                [this,
                 color    = *color,
                 depth    = *depth,
                 vertices = *vertices,
                 indices  = *indices,
                 size,
                 keep,
                 view = *build.view](VriCommandBuffer* cmd, RenderGraph& resources) -> std::expected<void, Error>
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
                    colorAttachment.loadOp  = keep ? VriAttachmentLoadOp_Load : VriAttachmentLoadOp_Clear;
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
                    const VriVertexBufferBinding binding {resources.getBuffer(vertices).handle, 0};
                    m_Device.core.CmdSetVertexBuffers(cmd, 0, &binding, 1);
                    m_Device.core.CmdSetIndexBuffer(cmd, resources.getBuffer(indices).handle, 0, VriIndexType_UInt32);
                    for (uint32_t index = 0; index < m_Scene->primitives.size(); ++index)
                    {
                        const auto&      primitive = m_Scene->primitives[index];
                        const auto&      world     = m_Scene->primitiveTransform(index);
                        const Parameters parameters {m_ViewProjections[view] * world, world};
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

        std::expected<void, vultra::Error> prepare(vultra::RenderGraph&, const vultra::StageFrame& frame) override
        {
            const auto& camera             = frame.cameras[*frame.view];
            m_ViewProjections[*frame.view] = camera.projection * camera.view;
            return {};
        }

    private:
        struct Parameters
        {
            glm::mat4 clipFromLocal;
            glm::mat4 worldFromLocal;
        };

        vultra::Device&                         m_Device;
        vultra::GpuScene*                       m_Scene  = nullptr;
        VriPipelineLayout*                      m_Layout = nullptr;
        std::unique_ptr<vultra::ShaderPipeline> m_Pipeline;
        std::vector<glm::mat4>                  m_ViewProjections;
    };

    // A frame stage: one clear of test.background, which every view then copies.
    class BackgroundStage final : public vultra::RenderStage
    {
    public:
        static constexpr std::array<float, 4> kColor {0.25f, 0.5f, 0.75f, 1};

        static vultra::StageType type()
        {
            using namespace vultra;
            return {{"test.background",
                     {"background"},
                     StageScope::eFrame,
                     {},
                     {{"test.background", VriFormat_RGBA16_SFLOAT, "color.linear.hdr"}}},
                    [](Device& device, const AssetSource*) -> std::expected<std::unique_ptr<RenderStage>, Error>
                    {
                        return std::make_unique<BackgroundStage>(device);
                    }};
        }

        explicit BackgroundStage(vultra::Device& device) :
            m_Device(device)
        {
        }

        std::expected<vultra::SceneResources, vultra::Error> addPasses(vultra::StageBuild& build) override
        {
            using namespace vultra;
            const auto texture = build.graph.createTexture(
                build.graphName("test.background"),
                support::clearedTexture(build.views.front().size, kColor, VriFormat_RGBA16_SFLOAT));
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            const auto added = build.graph.addPass(
                build.graphName("Background"),
                {{*texture, Usage::eColorWrite, true}},
                [this, target = *texture](VriCommandBuffer* cmd, RenderGraph& resources) -> std::expected<void, Error>
                {
                    auto&      texture = resources.getTexture(target);
                    const auto view    = texture.view();
                    if (!view)
                    {
                        return std::unexpected(view.error());
                    }
                    beginColorPass(m_Device, cmd, *view, {texture.desc.width, texture.desc.height}, kColor.data());
                    m_Device.core.CmdEndRendering(cmd);
                    return {};
                });
            if (!added)
            {
                return std::unexpected(added.error());
            }
            return SceneResources {{"test.background", *texture}};
        }

        std::expected<void, vultra::Error> prepare(vultra::RenderGraph&, const vultra::StageFrame& frame) override
        {
            require(!frame.view && frame.cameras.size() >= 1, "A frame stage was prepared for a single view");
            return {};
        }

    private:
        vultra::Device& m_Device;
    };

    vultra::SceneData quadScene()
    {
        vultra::SceneData scene;
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
        return scene;
    }

    vultra::RenderCamera quadCamera(glm::vec3 eye)
    {
        return {glm::lookAtRH(eye, glm::vec3(0), glm::vec3(0, 1, 0)),
                glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                0.1f,
                10};
    }

    // Renders one frame of a path for these views and returns each view's scene.hdr.
    std::vector<vultra::Image> renderViews(vultra::Device&                       device,
                                           vultra::RenderPath&                   path,
                                           vultra::GpuScene&                     scene,
                                           vultra::Environment&                  environment,
                                           std::span<const vultra::RenderView>   views,
                                           std::span<const vultra::RenderCamera> cameras,
                                           std::vector<std::string>*             passes = nullptr)
    {
        using namespace vultra;
        RenderGraph graph(device);
        const auto  resources = must(path.addPasses(graph, scene, environment, views));
        require(resources.size() == views.size(), "A render path returned resources for another number of views");
        std::vector<RenderGraph::Resource> outputs;
        for (const auto& view : resources)
        {
            outputs.push_back(must(findSceneResource(view, kSceneHdr)));
            must(graph.exportResource(outputs.back()));
        }
        must(graph.compile());
        must(path.prepare(graph, cameras, {}));
        const auto frame = must(Frame::create(device));
        run(*frame, graph);
        std::vector<Image> images;
        for (const auto output : outputs)
        {
            images.push_back(must(readback(device, graph.getTexture(output))));
            finite(images.back());
        }
        if (passes)
        {
            *passes = graph.activePasses();
        }
        return images;
    }

    // The same scene, camera and consumer code run through two render paths selected at runtime.
    void
    switchRenderPaths(vultra::Device& device, vultra::Environment& environment, const std::filesystem::path& directory)
    {
        using namespace vultra;
        const auto         gpu     = must(GpuScene::create(device, quadScene()));
        const RenderCamera camera  = quadCamera({0, 0, 3});
        const auto         forward = must(createForwardPath(device));
        auto*              shading = static_cast<ForwardShadingStage*>(forward->stage("shading"));
        const std::array   stages  = {meshStage(), NormalStage::type(directory)};
        const auto         normals =
            must(RenderPath::create(device,
                                    {"test.normals", {{"geometry", "vultra.mesh"}, {"shading", "test.normal_shading"}}},
                                    stages));
        shading->settings.ibl                      = false;
        shading->settings.skybox                   = false;
        constexpr size_t                    center = (32 * 65 + 32) * 4;
        std::array<std::array<float, 3>, 2> colors {};
        std::array<float, 2>                depths {};
        const std::array<RenderPath*, 2>    paths {forward.get(), normals.get()};
        const RenderView                    view {"main", {65, 65}};
        for (size_t index = 0; index < paths.size(); ++index)
        {
            RenderGraph graph(device);
            const auto resources = must(paths[index]->addPasses(graph, *gpu, environment, std::span(&view, 1))).front();
            const auto hdr       = must(findSceneResource(resources, kSceneHdr));
            const auto depth     = must(findSceneResource(resources, kSceneDepth));
            must(graph.exportResource(hdr));
            must(graph.exportResource(depth));
            must(graph.compile());
            must(paths[index]->prepare(graph, std::span(&camera, 1), {}));
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

        // A stage must return exactly what its description provides.
        struct Incomplete final : RenderStage
        {
            std::expected<SceneResources, Error> addPasses(StageBuild&) override
            {
                return SceneResources {};
            }

            std::expected<void, Error> prepare(RenderGraph&, const StageFrame&) override
            {
                return {};
            }
        };

        auto incomplete   = NormalStage::type(directory);
        incomplete.create = [](Device&, const AssetSource*) -> std::expected<std::unique_ptr<RenderStage>, Error>
        {
            return std::make_unique<Incomplete>();
        };
        const std::array incompleteStages {meshStage(), incomplete};
        const auto       broken =
            must(RenderPath::create(device,
                                    {"test.broken", {{"geometry", "vultra.mesh"}, {"shading", "test.normal_shading"}}},
                                    incompleteStages));
        RenderGraph graph(device);
        support::rejects(broken->addPasses(graph, *gpu, environment, std::span(&view, 1)),
                         "A stage that returned none of the resources it provides passed");
        std::cout << "Render paths: forward and a custom normal stage share scene.hdr/scene.depth; contract checked\n";
    }

    // Validation reads descriptions only, so every problem is found without a device.
    void pathValidation()
    {
        using namespace vultra;
        const auto texture = [](std::string name)
        {
            return SceneResourceDesc {std::move(name), VriFormat_RGBA16_SFLOAT, "color.linear.hdr"};
        };
        const std::vector<StageDesc> stages {
            meshStage().desc,
            forwardShadingStage(Device::graphicsAPIs().front()).desc,
            toneMappingStage().desc,
            {"test.gbuffer",
             {"visibility", "surface"},
             StageScope::eView,
             {{std::string(kSceneVertices)}},
             {{std::string(kSceneDepth), VriFormat_D32_SFLOAT, "depth"}, texture("scene.normal")}},
            {"test.direct",
             {"direct"},
             StageScope::eView,
             {{"scene.normal"}, {"scene.shadow_mask", true}},
             {texture("scene.direct")}},
            {"test.compose", {"compose"}, StageScope::eView, {{"scene.direct"}}, {texture(std::string(kSceneHdr))}},
            {"test.frame_blur", {"blur"}, StageScope::eFrame, {{"scene.direct"}}, {texture("test.blurred")}},
            {"test.tlas", {"tlas"}, StageScope::eFrame, {}, {{"scene.tlas", VriFormat_Unknown, {}, true}}},
            {"test.surface_cache", {"indirect"}, StageScope::eView, {{"scene.tlas"}}, {texture("scene.indirect")}},
            {"test.lights",
             {"lighting"},
             StageScope::eView,
             {{"scene.lights"}, {"camera.data"}},
             {texture("test.lit")}},
            {"test.own_lights", {"lights"}, StageScope::eFrame, {}, {{"scene.lights", VriFormat_Unknown, {}, true}}}};
        const auto issues = [&](std::vector<PathSlot> slots)
        {
            return validateRenderPath({"test.path", std::move(slots)}, stages);
        };
        const auto expect = [&](std::vector<PathSlot> slots, PathIssueKind kind, size_t slot, std::string_view text)
        {
            const auto found = issues(std::move(slots));
            const bool match = std::ranges::any_of(found,
                                                   [&](const PathIssue& issue)
                                                   {
                                                       return issue.kind == kind && issue.slot == slot &&
                                                              issue.message.find(text) != std::string::npos;
                                                   });
            if (!match)
            {
                for (const auto& issue : found)
                {
                    std::cerr << "  slot " << issue.slot << ": " << issue.message << '\n';
                }
                support::fail("Render path validation missed: " + std::string(text));
            }
        };

        require(validateRenderPath(forwardPathDesc(), stages).empty(), "vultra.forward did not validate");
        const std::vector<PathSlot> deferred {{"geometry", "vultra.mesh"},
                                              {"visibility", "test.gbuffer"},
                                              {"surface", "test.gbuffer"},
                                              {"direct", "test.direct"},
                                              {"compose", "test.compose"}};
        require(issues(deferred).empty(), "A path with a two-slot stage and an optional need did not validate");
        // Without a stage that provides scene.output, the display slot reads scene.hdr; it must come last.
        auto displayed = deferred;
        displayed.push_back({std::string(kDisplaySlot), "vultra.tone_mapping"});
        require(issues(displayed).empty(), "The display slot did not read scene.hdr as scene.output");
        displayed.insert(displayed.begin() + 4, {std::string(kDisplaySlot), "vultra.tone_mapping"});
        displayed.pop_back();
        expect(displayed, PathIssueKind::eDisplayNotLast, 4, "must be the last one");

        expect({{"geometry", "vultra.mesh"}, {"shading", "test.missing"}},
               PathIssueKind::eUnknownStage,
               1,
               "unknown stage 'test.missing'");
        expect({{"shading", "vultra.mesh"}}, PathIssueKind::eWrongSlot, 0, "it fills 'geometry'");
        expect({{"geometry", "vultra.mesh"}, {"visibility", "test.gbuffer"}, {"direct", "test.direct"}},
               PathIssueKind::ePartialStage,
               1,
               "fills the slots 'visibility', 'surface' together");
        expect({{"geometry", "vultra.mesh"}, {"geometry", "vultra.mesh"}, {"shading", "vultra.forward_shading"}},
               PathIssueKind::eDuplicateSlot,
               1,
               "slot 'geometry' appears twice");
        // VG011 names who could provide the missing resource.
        expect({{"geometry", "vultra.mesh"}, {"shading", "vultra.forward_shading"}, {"indirect", "test.surface_cache"}},
               PathIssueKind::eMissingNeed,
               2,
               "stage 'test.surface_cache' (indirect) needs 'scene.tlas', which nothing before it provides; stage "
               "'test.tlas' provides it");
        expect({{"shading", "vultra.forward_shading"}, {"geometry", "vultra.mesh"}},
               PathIssueKind::eMissingNeed,
               0,
               "needs 'scene.vertices'");
        expect({{"geometry", "vultra.mesh"}, {"visibility", "test.gbuffer"}, {"surface", "test.gbuffer"}},
               PathIssueKind::eMissingStandard,
               3,
               "does not provide 'scene.hdr'");
        expect({{"geometry", "vultra.mesh"},
                {"visibility", "test.gbuffer"},
                {"surface", "test.gbuffer"},
                {"shading", "vultra.forward_shading"}},
               PathIssueKind::eDuplicateProvide,
               3,
               "provides 'scene.depth', which stage 'test.gbuffer' (visibility) provides already");
        auto frameAfterView = deferred;
        frameAfterView.push_back({"blur", "test.frame_blur"});
        expect(frameAfterView, PathIssueKind::eFrameNeedsView, 5, "runs once per frame but needs 'scene.direct'");
        // scene.lights and camera.data come from the path itself, before any stage.
        auto withInputs = deferred;
        withInputs.push_back({"lighting", "test.lights"});
        require(issues(withInputs).empty(), "A stage could not read the path's own scene.lights and camera.data");
        expect({{"lights", "test.own_lights"}, {"geometry", "vultra.mesh"}, {"shading", "vultra.forward_shading"}},
               PathIssueKind::eDuplicateProvide,
               0,
               "provides 'scene.lights', which the render path itself provides already");
        std::cout << "Render path validation: every problem kind found from descriptions alone\n";
    }

    // One path, two views: frame stages run once, view stages once per view, and each view equals a
    // render of that view alone.
    void twoViews(vultra::Device& device, vultra::Environment& environment, const std::filesystem::path& directory)
    {
        using namespace vultra;
        const auto       gpu = must(GpuScene::create(device, quadScene()));
        const std::array views {RenderView {"left", {65, 65}}, RenderView {"right", {65, 65}}};
        const std::array cameras {quadCamera({-0.4f, 0.2f, 6}), quadCamera({0.4f, -0.3f, 7})};

        const auto forward                                                         = must(createForwardPath(device));
        static_cast<ForwardShadingStage*>(forward->stage("shading"))->settings.ibl = false;
        std::vector<std::string> passes;
        const auto               both = renderViews(device, *forward, *gpu, environment, views, cameras, &passes);
        require(passes == std::vector<std::string> {"left:Skybox", "left:Forward", "right:Skybox", "right:Forward"},
                "vultra.forward did not add its view passes once per view under the view's name");
        for (size_t view = 0; view < views.size(); ++view)
        {
            const RenderView alone {"main", views[view].size};
            const auto       single =
                renderViews(device, *forward, *gpu, environment, std::span(&alone, 1), std::span(&cameras[view], 1));
            require(single.front().rgba == both[view].rgba,
                    "A view of a two-view forward render differs from rendering it alone");
        }
        require(both[0].rgba != both[1].rgba, "Two views with different cameras rendered the same image");

        const std::array stages {meshStage(), BackgroundStage::type(), NormalStage::type(directory)};
        const auto       path   = must(RenderPath::create(
            device,
            {"test.background_normals",
                     {{"geometry", "vultra.mesh"}, {"background", "test.background"}, {"shading", "test.normal_shading"}}},
            stages));
        const auto       images = renderViews(device, *path, *gpu, environment, views, cameras, &passes);
        require(std::ranges::count(passes, "Background") == 1 && std::ranges::count(passes, "left:Normals") == 1 &&
                    std::ranges::count(passes, "right:Normals") == 1,
                "A frame stage pass must appear once and a view stage pass once per view");
        for (size_t view = 0; view < views.size(); ++view)
        {
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                require(images[view].rgba[channel] == BackgroundStage::kColor[channel],
                        "A view did not start from the frame stage's background");
            }
            const RenderView alone {"main", views[view].size};
            const auto       single =
                renderViews(device, *path, *gpu, environment, std::span(&alone, 1), std::span(&cameras[view], 1));
            require(single.front().rgba == images[view].rgba,
                    "A view of a two-view render with a frame stage differs from rendering it alone");
        }

        // The path's own inputs: one scene.lights for the frame and a camera.data per view, written by prepare().
        {
            RenderGraph graph(device);
            const auto  resources = must(forward->addPasses(graph, *gpu, environment, views));
            must(graph.compile());
            const auto read = [&](size_t view)
            {
                auto&      buffer = graph.getBuffer(must(findSceneResource(resources[view], kCameraData)));
                CameraData data {};
                auto*      mapped = device.core.MapBuffer(buffer.handle, 0, sizeof(data));
                require(mapped != nullptr, "Map camera.data");
                std::memcpy(&data, mapped, sizeof(data));
                device.core.UnmapBuffer(buffer.handle);
                return data;
            };
            require(must(findSceneResource(resources[0], kSceneLights)).index ==
                        must(findSceneResource(resources[1], kSceneLights)).index,
                    "scene.lights must be one frame resource shared by the views");
            must(forward->prepare(graph, cameras, {}));
            for (size_t view = 0; view < views.size(); ++view)
            {
                const auto data     = read(view);
                const auto expected = cameras[view].projection * cameras[view].view;
                require(data.viewProjection == expected && data.previousViewProjection == expected &&
                            data.imageSize == glm::vec2(65, 65) && data.nearPlane == cameras[view].nearPlane &&
                            data.farPlane == cameras[view].farPlane &&
                            glm::all(glm::epsilonEqual(glm::vec3(data.position),
                                                       glm::vec3(glm::inverse(cameras[view].view)[3]),
                                                       1e-5f)),
                        "camera.data does not hold the view's constants");
            }
            const std::array moved {quadCamera({0, 0, 5}), quadCamera({1, 1, 5})};
            must(forward->prepare(graph, moved, {}));
            for (size_t view = 0; view < views.size(); ++view)
            {
                const auto data = read(view);
                require(data.previousViewProjection == cameras[view].projection * cameras[view].view &&
                            data.viewProjection == moved[view].projection * moved[view].view,
                        "camera.data does not keep the previous frame's view projection");
            }
        }
        std::cout << "Two views: frame stage once, view stages per view, each view equal to its single render\n";
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
    gbufferLayers(device, environment);
    displayStage(device, environment);
    sceneLightBuffer(device, environment);
    materialReference(device, environment);
    materialTextures(device, environment, vultra::TextureFormat::eBc5Unorm);
    materialTextures(device, environment, vultra::TextureFormat::eRg8Unorm);
    tangentFramesAndEmission(device, environment);
    switchRenderPaths(device, environment, directory);
    pathValidation();
    twoViews(device, environment, directory);
    std::cout << "Forward path tests passed: constant HDR/IBL mip chain, BRDF LUT, upstream OpenPBR C++ reference, "
                 "material textures and live transforms\n";
    return 0;
}
