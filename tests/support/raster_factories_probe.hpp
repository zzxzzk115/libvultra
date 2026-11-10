#pragma once

#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/builtin/raster_passes.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cstdio>

namespace vultra::test
{
    inline std::expected<void, std::string> rasterFactoriesProbe(Device&            device,
                                                                 GpuScene&          scene,
                                                                 Environment&       environment,
                                                                 const AssetSource* shaders = nullptr)
    {
        auto created = BuiltinRenderer::create(device, scene, environment, VriFormat_RGBA8_UNORM, shaders);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        auto& renderer                     = **created;
        renderer.settings.path             = RenderPath::eNaiveDeferred;
        renderer.settings.shadowResolution = 32;
        renderer.settings.shadowFilter     = ShadowFilter::eDisabled;
        renderer.settings.ibl              = false;
        renderer.settings.skybox           = false;
        constexpr Extent size {17, 13};
        const auto       definitions = builtinRasterPassDefinitions();
        {
            PassCatalog incomplete(device);
            if (auto added = incomplete.addExpected(definitions.front()); !added)
            {
                return added;
            }
            BuiltinRenderer::Outputs bindings;
            if (bindBuiltinRasterPassesExpected(incomplete, renderer, bindings, size))
            {
                return std::unexpected("Raster binding accepted a missing stage definition");
            }
            RenderGraph graph(device);
            auto        rejected = incomplete.buildExpected(graph, "vultra.shadow", "shadow", {});
            if (rejected || rejected.error().find("bindBuiltinRasterPassesExpected") == std::string::npos)
            {
                return std::unexpected("Failed raster binding partially replaced an earlier factory");
            }
        }
        {
            auto foreign = Device::create(true, nullptr, 0);
            if (!foreign)
            {
                return std::unexpected(foreign.error());
            }
            auto& gpu     = **foreign;
            auto  storage = Buffer::create(gpu, {4, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
            if (!storage)
            {
                return std::unexpected(storage.error());
            }
            auto* value = static_cast<uint32_t*>(gpu.core.MapBuffer((*storage)->handle, 0, 4));
            if (!value)
            {
                return std::unexpected("Map raster append recovery prefix failed");
            }
            *value = 17;
            gpu.core.UnmapBuffer((*storage)->handle);
            RenderGraph graph(gpu);
            auto        prefix = graph.importResourceExpected("prefix", **storage);
            if (!prefix || !graph.exportResourceExpected(*prefix) || !graph.compileExpected())
            {
                return std::unexpected("Compile raster append recovery prefix failed");
            }
            const auto               before = graph.snapshot();
            BuiltinRenderer::Outputs bindings;
            PassCatalog              catalog(gpu);
            if (auto added = catalog.addDefinitionsExpected(definitions); !added)
            {
                return added;
            }
            if (auto bound = bindBuiltinRasterPassesExpected(catalog, renderer, bindings, size); !bound)
            {
                return bound;
            }
            // Depth declaration succeeds before the renderer rejects this graph's foreign device.
            auto rejected = catalog.buildExpected(graph, "vultra.gbuffer", "geometry", {});
            if (rejected || bindings.depth.graph || graph.snapshot().resources.size() != before.resources.size() ||
                graph.snapshot().passes.size() != before.passes.size())
            {
                return std::unexpected("Late raster append rejection published stale bindings or graph resources");
            }
        }
        BuiltinRenderer::Outputs stagedOutputs;
        PassCatalog              catalog(device);
        if (auto added = catalog.addDefinitionsExpected(definitions); !added)
        {
            return added;
        }
        if (auto bound = bindBuiltinRasterPassesExpected(catalog, renderer, stagedOutputs, size); !bound)
        {
            return bound;
        }
        GraphDefinition definition;
        definition.passes = {{"shadow", "vultra.shadow", {}},
                             {"sky", "vultra.skybox", {}},
                             {"geometry", "vultra.gbuffer", {}},
                             {"lighting", "vultra.deferred_lighting", {}}};
        definition.edges  = {{"sky.hdr", "lighting.hdr"}, {"geometry.depth", "lighting.depth"}};
        constexpr std::array names {"position_metallic",
                                    "normal_roughness",
                                    "albedo_weight",
                                    "emission_occlusion",
                                    "specular",
                                    "geometric_normal_ior",
                                    "coat"};
        for (const auto name : names)
        {
            definition.edges.push_back({"geometry." + std::string(name), "lighting." + std::string(name)});
        }
        for (unsigned cascade = 0; cascade < 4; ++cascade)
        {
            const auto name = "cascade" + std::to_string(cascade);
            definition.edges.push_back({"shadow." + name, "lighting." + name});
        }
        definition.outputs = {"lighting.hdr", "geometry.depth"};
        GraphBuild  built;
        RenderGraph staged(device);
        auto        appended = definition.buildExpected(staged, catalog, {});
        if (!appended)
        {
            return std::unexpected(appended.error());
        }
        built = std::move(*appended);
        if (built.passes.size() != 4 || built.outputs.size() != 2 || built.outputs[0].resource.graph != &staged ||
            built.outputs[1].resource.graph != &staged || stagedOutputs.hdr.graph != &staged ||
            stagedOutputs.depth.graph != &staged || built.outputs[0].resource.index != stagedOutputs.hdr.index ||
            built.outputs[1].resource.index != stagedOutputs.depth.index)
        {
            return std::unexpected("Raster stage bindings disagree with their compiled definition outputs");
        }
        if (auto compiled = staged.compileExpected(); !compiled)
        {
            return compiled;
        }
        RenderGraph direct(device);
        auto        directOutputs = renderer.addScenePassesExpected(direct, size);
        if (!directOutputs || !direct.exportResourceExpected(directOutputs->hdr) ||
            !direct.exportResourceExpected(directOutputs->depth) || !direct.compileExpected())
        {
            return std::unexpected("Build direct raster comparison failed");
        }
        RenderCamera camera;
        camera.nearPlane = scene.radius * 0.01f;
        camera.farPlane  = scene.radius * 20;
        camera.view =
            glm::lookAtRH(scene.center + glm::vec3(0, 0, scene.radius * 2.5f), scene.center, glm::vec3(0, 1, 0));
        camera.projection = glm::perspectiveRH_ZO(glm::radians(60.0f),
                                                  float(size.width) / size.height,
                                                  camera.nearPlane,
                                                  camera.farPlane);
        auto frame        = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const auto render =
            [&](RenderGraph&                    graph,
                const BuiltinRenderer::Outputs& outputs) -> std::expected<std::array<Image, 2>, std::string>
        {
            if (auto prepared = renderer.prepareExpected(camera, graph, outputs); !prepared)
            {
                return std::unexpected(prepared.error());
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto recorded = graph.executeExpected(*commands); !recorded)
            {
                return std::unexpected(recorded.error());
            }
            if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
            {
                return std::unexpected(completed.error());
            }
            renderer.completeFrame();
            std::array<Image, 2> images;
            const std::array     selected {outputs.hdr, outputs.depth};
            for (size_t index = 0; index < selected.size(); ++index)
            {
                auto texture = graph.getTextureExpected(selected[index]);
                if (!texture)
                {
                    return std::unexpected(texture.error());
                }
                auto image = readbackExpected(device, **texture);
                if (!image)
                {
                    return std::unexpected(image.error());
                }
                images[index] = std::move(*image);
            }
            return images;
        };
        auto reference = render(direct, *directOutputs);
        auto candidate = render(staged, stagedOutputs);
        if (!reference || !candidate)
        {
            return std::unexpected(reference ? candidate.error() : reference.error());
        }
        bool geometryVisible = false;
        for (size_t index = 0; index < (*reference)[1].rgba.size(); index += 4)
        {
            const auto depth = (*reference)[1].rgba[index];
            geometryVisible |= depth >= 0 && depth < 1;
        }
        if (!geometryVisible)
        {
            return std::unexpected("Raster factory comparison did not render visible geometry");
        }
        for (size_t index = 0; index < reference->size(); ++index)
        {
            if ((*candidate)[index].size != size || (*candidate)[index].rgba != (*reference)[index].rgba)
            {
                return std::unexpected("Expected raster factories changed direct HDR/depth pixels");
            }
        }
        std::puts(
            "EXPECTED_RASTER_FACTORIES stages=4 matched_channels=1768 late_failure=preserved binding_rejection=atomic");
        return {};
    }
} // namespace vultra::test
