#include <vultra/render/environment.hpp>
#include <vultra/render/render_path.hpp>
#include <vultra/render/scene.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>

namespace vultra
{
    namespace
    {
        // Consecutive entries naming the same stage are one stage that fills several slots.
        std::vector<std::vector<size_t>> groupSlots(const RenderPathDesc& path)
        {
            std::vector<std::vector<size_t>> groups;
            for (size_t index = 0; index < path.slots.size(); ++index)
            {
                if (groups.empty() || path.slots[groups.back().front()].stage != path.slots[index].stage)
                {
                    groups.emplace_back();
                }
                groups.back().push_back(index);
            }
            return groups;
        }

        const StageDesc* findStage(std::span<const StageDesc> stages, std::string_view name)
        {
            const auto found = std::ranges::find(stages, name, &StageDesc::name);
            return found == stages.end() ? nullptr : &*found;
        }

        // The path's own inputs are recorded with this slot.
        constexpr size_t kPathSlot = SIZE_MAX;

        const std::array<PathInput, 6> kPathInputs {
            PathInput {{std::string(kSceneLights), VriFormat_Unknown, "lights", true, sizeof(GpuLight)},
                       StageScope::eFrame},
            PathInput {{std::string(kSceneEnvironment), VriFormat_RGBA16_SFLOAT, "environment.radiance"},
                       StageScope::eFrame},
            PathInput {{std::string(kSceneEnvironmentDiffuse), VriFormat_RGBA16_SFLOAT, "environment.diffuse"},
                       StageScope::eFrame},
            PathInput {{std::string(kSceneEnvironmentSpecular), VriFormat_RGBA16_SFLOAT, "environment.specular"},
                       StageScope::eFrame},
            PathInput {{std::string(kSceneEnvironmentBrdf), VriFormat_RGBA16_SFLOAT, "environment.brdf"},
                       StageScope::eFrame},
            PathInput {{std::string(kCameraData), VriFormat_Unknown, "camera", true, sizeof(CameraData)},
                       StageScope::eView}};

        bool fillsDisplay(const RenderPathDesc& path, std::span<const size_t> slots)
        {
            return std::ranges::any_of(slots,
                                       [&path](size_t index)
                                       {
                                           return path.slots[index].slot == kDisplaySlot;
                                       });
        }

        std::string describe(const RenderPathDesc& path, size_t slot)
        {
            if (slot == kPathSlot)
            {
                return "the render path itself";
            }
            return "stage '" + path.slots[slot].stage + "' (" + path.slots[slot].slot + ")";
        }

        std::expected<void, Error> checkProvided(const StageDesc&               desc,
                                                 std::span<const SceneResource> resources,
                                                 std::span<const std::string>   requested,
                                                 const RenderGraph&             graph)
        {
            for (const auto& returned : resources)
            {
                if (std::ranges::find(desc.provides, returned.name, &SceneResourceDesc::name) == desc.provides.end() ||
                    std::ranges::count(resources, returned.name, &SceneResource::name) != 1)
                {
                    return makeError("Stage {} returns {}, which it does not provide once", desc.name, returned.name);
                }
            }
            for (const auto& provided : desc.provides)
            {
                const auto resource = findSceneResource(resources, provided.name);
                if (!resource)
                {
                    if (std::ranges::find(requested, provided.name) == requested.end())
                    {
                        continue;
                    }
                    return makeError("Stage {} does not return {}, which is requested", desc.name, provided.name);
                }
                const auto info = graph.resourceInfo(*resource);
                if (!info)
                {
                    return std::unexpected(info.error());
                }
                // A texture declared without a format, such as final, takes the one the caller asks for.
                const bool matches = provided.buffer ? !info->isTexture :
                                                       info->isTexture && (provided.format == VriFormat_Unknown ||
                                                                           info->textureDesc.format == provided.format);
                if (!matches)
                {
                    return makeError("Stage {} returns {} with another format than it declares",
                                     desc.name,
                                     provided.name);
                }
            }
            return {};
        }
    } // namespace

    std::span<const SceneResourceDesc> gbufferLayers(VriGraphicsAPI api)
    {
        // Keep in sync with vultra.gbuffer and the GBufferOutput of forward.slang and skybox.slang.
        static const std::vector<SceneResourceDesc> compact {
            {std::string(kSceneNormal), VriFormat_RG16_SFLOAT, "normal.world"},
            {std::string(kSceneAlbedo), VriFormat_RGBA8_SRGB, "color.albedo"},
            {std::string(kSceneMaterial), VriFormat_RG8_UNORM, "material.surface"},
            {std::string(kSceneEmission), VriFormat_R32_UINT, "color.emission"},
            {std::string(kSceneMotion), VriFormat_RG16_SFLOAT, "motion.screen"}};
        static const std::vector<SceneResourceDesc> full {
            {std::string(kSceneNormal), VriFormat_RG16_SFLOAT, "normal.world"},
            {std::string(kSceneAlbedo), VriFormat_RGBA8_SRGB, "color.albedo"},
            {std::string(kSceneMaterial), VriFormat_RGBA16_UNORM, "material.surface"},
            {std::string(kSceneEmission), VriFormat_RGBA16_SFLOAT, "color.emission"},
            {std::string(kSceneMotion), VriFormat_RG16_SFLOAT, "motion.screen"},
            {std::string(kSceneCoat), VriFormat_RGBA16_SFLOAT, "material.coat"},
            {std::string(kSceneSpecular), VriFormat_RGBA16_SFLOAT, "material.specular"}};
        return api == VriGraphicsAPI_WebGPU ? compact : full;
    }

    std::expected<RenderGraph::Resource, Error> findSceneResource(std::span<const SceneResource> resources,
                                                                  std::string_view               name)
    {
        const auto found = std::ranges::find(resources, name, &SceneResource::name);
        if (found == resources.end())
        {
            return makeError("Render path does not provide {}", name);
        }
        return found->resource;
    }

    std::span<const PathInput> pathInputs()
    {
        return kPathInputs;
    }

    bool StageBuild::requests(std::string_view name) const
    {
        return std::ranges::find(requested, name) != requested.end();
    }

    std::string StageBuild::graphName(std::string_view name) const
    {
        if (!view || views.size() < 2)
        {
            return std::string(name);
        }
        return views[*view].name + ":" + std::string(name);
    }

    std::vector<PathIssue> validateRenderPath(const RenderPathDesc& path, std::span<const StageDesc> stages)
    {
        std::vector<PathIssue> issues;
        for (size_t index = 0; index < path.slots.size(); ++index)
        {
            for (size_t earlier = 0; earlier < index; ++earlier)
            {
                if (path.slots[earlier].slot == path.slots[index].slot)
                {
                    issues.push_back(
                        {PathIssueKind::eDuplicateSlot, index, "slot '" + path.slots[index].slot + "' appears twice"});
                    break;
                }
            }
        }

        struct Provider
        {
            StageScope scope;
            size_t     slot;
        };

        std::map<std::string, Provider, std::less<>> provided;
        for (const auto& input : kPathInputs)
        {
            provided.emplace(input.desc.name, Provider {input.scope, kPathSlot});
        }
        const auto groups = groupSlots(path);
        for (const auto& group : groups)
        {
            const size_t first = group.front();
            const auto*  desc  = findStage(stages, path.slots[first].stage);
            if (fillsDisplay(path, group))
            {
                if (&group != &groups.back())
                {
                    issues.push_back({PathIssueKind::eDisplayNotLast,
                                      first,
                                      describe(path, first) + " fills the display slot, which must be the last one"});
                }
                // Without a stage that provides scene.output, the display shows scene.hdr.
                const auto hdr = provided.find(kSceneHdr);
                if (!provided.contains(kSceneOutput) && hdr != provided.end())
                {
                    provided.emplace(std::string(kSceneOutput), hdr->second);
                }
            }
            if (!desc)
            {
                issues.push_back(
                    {PathIssueKind::eUnknownStage,
                     first,
                     "unknown stage '" + path.slots[first].stage + "' in slot '" + path.slots[first].slot + "'"});
                continue;
            }
            bool fits = true;
            for (const size_t index : group)
            {
                if (std::ranges::find(desc->slots, path.slots[index].slot) == desc->slots.end())
                {
                    std::string slots;
                    for (const auto& slot : desc->slots)
                    {
                        slots += (slots.empty() ? "'" : ", '") + slot + "'";
                    }
                    issues.push_back({PathIssueKind::eWrongSlot,
                                      index,
                                      describe(path, index) + " cannot fill this slot; it fills " + slots});
                    fits = false;
                }
            }
            if (!fits)
            {
                continue;
            }
            if (!std::ranges::equal(group,
                                    desc->slots,
                                    {},
                                    [&](size_t index)
                                    {
                                        return path.slots[index].slot;
                                    }))
            {
                std::string slots;
                for (const auto& slot : desc->slots)
                {
                    slots += (slots.empty() ? "'" : ", '") + slot + "'";
                }
                issues.push_back(
                    {PathIssueKind::ePartialStage,
                     first,
                     "stage '" + desc->name + "' fills the slots " + slots + " together and in this order"});
                continue;
            }

            for (const auto& need : desc->needs)
            {
                const auto found = provided.find(need.name);
                if (found == provided.end())
                {
                    if (need.optional)
                    {
                        continue;
                    }
                    std::string message =
                        describe(path, first) + " needs '" + need.name + "', which nothing before it provides";
                    for (const auto& other : stages)
                    {
                        if (std::ranges::find(other.provides, need.name, &SceneResourceDesc::name) !=
                            other.provides.end())
                        {
                            message += "; stage '" + other.name + "' provides it";
                            break;
                        }
                    }
                    issues.push_back({PathIssueKind::eMissingNeed, first, std::move(message)});
                }
                else if (desc->scope == StageScope::eFrame && found->second.scope == StageScope::eView)
                {
                    issues.push_back({PathIssueKind::eFrameNeedsView,
                                      first,
                                      describe(path, first) + " runs once per frame but needs '" + need.name +
                                          "', which " + describe(path, found->second.slot) +
                                          " provides for each view"});
                }
            }
            for (const auto& resource : desc->provides)
            {
                const auto [found, added] = provided.try_emplace(resource.name, Provider {desc->scope, first});
                if (!added)
                {
                    issues.push_back({PathIssueKind::eDuplicateProvide,
                                      first,
                                      describe(path, first) + " provides '" + resource.name + "', which " +
                                          describe(path, found->second.slot) + " provides already"});
                }
            }
        }
        for (const auto required : {kSceneHdr, kSceneDepth})
        {
            if (!provided.contains(required))
            {
                issues.push_back({PathIssueKind::eMissingStandard,
                                  path.slots.size(),
                                  "render path '" + path.name + "' does not provide '" + std::string(required) + "'"});
            }
        }
        return issues;
    }

    RenderPath::RenderPath(Device& device, RenderPathDesc desc) :
        m_Device(device),
        m_Desc(std::move(desc))
    {
    }

    std::expected<std::unique_ptr<RenderPath>, Error> RenderPath::create(Device&                    device,
                                                                         RenderPathDesc             desc,
                                                                         std::span<const StageType> stages,
                                                                         const AssetSource*         shaders)
    {
        std::vector<StageDesc> descs;
        for (const auto& type : stages)
        {
            descs.push_back(type.desc);
        }
        if (const auto issues = validateRenderPath(desc, descs); !issues.empty())
        {
            return makeError("Render path {}: {}", desc.name, issues.front().message);
        }
        std::unique_ptr<RenderPath> path(new RenderPath(device, std::move(desc)));
        auto                        lights = LightBuffer::create(device);
        if (!lights)
        {
            return std::unexpected(lights.error());
        }
        path->m_Lights = std::move(*lights);
        for (auto& group : groupSlots(path->m_Desc))
        {
            const auto& type = *std::ranges::find(stages,
                                                  path->m_Desc.slots[group.front()].stage,
                                                  [](const StageType& candidate) -> const std::string&
                                                  {
                                                      return candidate.desc.name;
                                                  });
            if (!type.create)
            {
                return makeError("Stage {} has no factory", type.desc.name);
            }
            auto stage = type.create(device, shaders);
            if (!stage)
            {
                return makeError("Create stage {}: {}", type.desc.name, stage.error().message);
            }
            std::vector<double> parameters;
            for (const auto& parameter : type.desc.parameters)
            {
                parameters.push_back(parameter.defaultValue);
            }
            path->m_Stages.push_back({type.desc, std::move(group), std::move(*stage), std::move(parameters)});
        }
        for (const auto& input : kPathInputs)
        {
            path->m_Provides.push_back(input.desc);
        }
        for (const auto scope : {StageScope::eFrame, StageScope::eView})
        {
            for (const auto& instance : path->m_Stages)
            {
                // final is the display's, after the project's graph.
                if (instance.desc.scope == scope && !fillsDisplay(path->m_Desc, instance.slots))
                {
                    path->m_Provides.insert(path->m_Provides.end(),
                                            instance.desc.provides.begin(),
                                            instance.desc.provides.end());
                }
            }
        }
        if (std::ranges::find(path->m_Provides, kSceneOutput, &SceneResourceDesc::name) == path->m_Provides.end())
        {
            path->m_Provides.push_back({std::string(kSceneOutput), VriFormat_RGBA16_SFLOAT, "color.linear.hdr"});
        }
        return path;
    }

    const RenderPathDesc& RenderPath::desc() const
    {
        return m_Desc;
    }

    std::span<const SceneResourceDesc> RenderPath::provides() const
    {
        return m_Provides;
    }

    RenderStage* RenderPath::stage(std::string_view slot) const
    {
        for (const auto& instance : m_Stages)
        {
            for (const size_t index : instance.slots)
            {
                if (m_Desc.slots[index].slot == slot)
                {
                    return instance.stage.get();
                }
            }
        }
        return nullptr;
    }

    std::expected<std::vector<SceneResources>, Error> RenderPath::addPasses(RenderGraph&                 graph,
                                                                            GpuScene&                    scene,
                                                                            Environment&                 environment,
                                                                            std::span<const RenderView>  views,
                                                                            std::span<const std::string> requested)
    {
        if (views.empty())
        {
            return makeError("Render path {} needs at least one view", m_Desc.name);
        }
        for (size_t index = 0; index < views.size() && views.size() > 1; ++index)
        {
            if (views[index].name.empty() || std::ranges::count(views, views[index].name, &RenderView::name) != 1)
            {
                return makeError("Render path {}: several views need distinct, non-empty names", m_Desc.name);
            }
        }
        m_ViewCount = 0;
        // What each stage must return: the standard outputs, what the caller reads and what later stages need.
        std::vector<std::vector<std::string>> requests(m_Stages.size());
        std::vector<std::string>              later(requested.begin(), requested.end());
        later.emplace_back(kSceneHdr);
        later.emplace_back(kSceneDepth);
        for (size_t index = m_Stages.size(); index-- > 0;)
        {
            requests[index] = later;
            for (const auto& need : m_Stages[index].desc.needs)
            {
                later.push_back(need.name);
            }
        }
        // The path's own inputs come first: the lights of the frame and the constants of each view.
        SceneResources frame;
        const auto     lights = graph.importResource(std::string(kSceneLights), m_Lights->buffer());
        if (!lights)
        {
            return std::unexpected(lights.error());
        }
        frame.push_back({std::string(kSceneLights), *lights});
        const std::array<std::pair<std::string_view, Texture*>, 4> environmentTextures {
            std::pair {kSceneEnvironment, environment.radiance.get()},
            std::pair {kSceneEnvironmentDiffuse, environment.diffuse.get()},
            std::pair {kSceneEnvironmentSpecular, environment.specular.get()},
            std::pair {kSceneEnvironmentBrdf, environment.brdfLut.get()}};
        for (const auto& [name, texture] : environmentTextures)
        {
            const auto imported = graph.importResource(std::string(name), *texture);
            if (!imported)
            {
                return std::unexpected(imported.error());
            }
            frame.push_back({std::string(name), *imported});
        }
        std::vector<SceneResources> perView(views.size());
        for (uint32_t view = 0; view < views.size(); ++view)
        {
            if (m_Views.size() <= view)
            {
                auto camera = Buffer::create(
                    m_Device,
                    VriBufferDesc {sizeof(CameraData), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
                if (!camera)
                {
                    return std::unexpected(camera.error());
                }
                m_Views.push_back({std::make_unique<Buffer>(std::move(*camera))});
            }
            // A new graph starts without motion: its first frame has no previous camera.
            m_Views[view].size = views[view].size;
            m_Views[view].previousViewProjection.reset();
            const StageBuild naming {graph, scene, environment, views, view, {}};
            const auto       camera = graph.importResource(naming.graphName(kCameraData), *m_Views[view].camera);
            if (!camera)
            {
                return std::unexpected(camera.error());
            }
            perView[view].push_back({std::string(kCameraData), *camera});
        }
        for (size_t index = 0; index < m_Stages.size(); ++index)
        {
            auto& instance = m_Stages[index];
            // The display stage follows the project's graph: addDisplay() adds it.
            instance.built = !fillsDisplay(m_Desc, instance.slots);
            if (!instance.built)
            {
                continue;
            }
            if (instance.desc.scope == StageScope::eFrame)
            {
                StageBuild build {graph, scene, environment, views, std::nullopt, frame, requests[index]};
                auto       added = instance.stage->addPasses(build);
                if (!added)
                {
                    return std::unexpected(added.error());
                }
                if (auto valid = checkProvided(instance.desc, *added, requests[index], graph); !valid)
                {
                    return std::unexpected(valid.error());
                }
                frame.insert(frame.end(), added->begin(), added->end());
                continue;
            }
            for (uint32_t view = 0; view < views.size(); ++view)
            {
                SceneResources visible = frame;
                visible.insert(visible.end(), perView[view].begin(), perView[view].end());
                StageBuild build {graph, scene, environment, views, view, visible, requests[index]};
                auto       added = instance.stage->addPasses(build);
                if (!added)
                {
                    return std::unexpected(added.error());
                }
                if (auto valid = checkProvided(instance.desc, *added, requests[index], graph); !valid)
                {
                    return std::unexpected(valid.error());
                }
                perView[view].insert(perView[view].end(), added->begin(), added->end());
            }
        }
        for (auto& resources : perView)
        {
            // Without a stage that provides scene.output, it is scene.hdr.
            if (!findSceneResource(resources, kSceneOutput))
            {
                const auto hdr = findSceneResource(resources, kSceneHdr);
                if (!hdr)
                {
                    return std::unexpected(hdr.error());
                }
                resources.push_back({std::string(kSceneOutput), *hdr});
            }
            resources.insert(resources.begin(), frame.begin(), frame.end());
        }
        m_ViewCount   = views.size();
        m_ViewDescs   = {views.begin(), views.end()};
        m_Scene       = &scene;
        m_Environment = &environment;
        return perView;
    }

    std::expected<std::vector<RenderGraph::Resource>, Error>
    RenderPath::addDisplay(RenderGraph& graph, std::span<const RenderGraph::Resource> outputs, VriFormat format)
    {
        const auto display = std::ranges::find_if(m_Stages,
                                                  [this](const Instance& instance)
                                                  {
                                                      return fillsDisplay(m_Desc, instance.slots);
                                                  });
        if (display == m_Stages.end())
        {
            return makeError("Render path {} has no {} slot", m_Desc.name, kDisplaySlot);
        }
        if (m_ViewCount == 0 || outputs.size() != m_ViewCount || !m_Scene || !m_Environment)
        {
            return makeError("Render path {} adds its display after addPasses, with one image per view", m_Desc.name);
        }
        const std::array<std::string, 1>   requested {std::string(kFinal)};
        std::vector<RenderGraph::Resource> finals;
        for (uint32_t view = 0; view < m_ViewCount; ++view)
        {
            const SceneResources shown {{std::string(kSceneOutput), outputs[view]}};
            StageBuild           build {graph, *m_Scene, *m_Environment, m_ViewDescs, view, shown, requested, format};
            auto                 added = display->stage->addPasses(build);
            if (!added)
            {
                return std::unexpected(added.error());
            }
            if (auto valid = checkProvided(display->desc, *added, requested, graph); !valid)
            {
                return std::unexpected(valid.error());
            }
            const auto final = findSceneResource(*added, kFinal);
            if (!final)
            {
                return std::unexpected(final.error());
            }
            const auto info = graph.resourceInfo(*final);
            if (!info || !info->isTexture || info->textureDesc.format != format)
            {
                return makeError("Stage {} does not give final in the display format", display->desc.name);
            }
            finals.push_back(*final);
        }
        display->built = true;
        return finals;
    }

    std::expected<double, Error> RenderPath::parameter(std::string_view slot, std::string_view name) const
    {
        for (const auto& instance : m_Stages)
        {
            for (const size_t index : instance.slots)
            {
                if (m_Desc.slots[index].slot != slot)
                {
                    continue;
                }
                const auto found = std::ranges::find(instance.desc.parameters, name, &PassParameter::name);
                if (found != instance.desc.parameters.end())
                {
                    return instance.parameters[size_t(found - instance.desc.parameters.begin())];
                }
            }
        }
        return makeError("Render path {} has no parameter {}.{}", m_Desc.name, slot, name);
    }

    std::expected<void, Error> RenderPath::setParameter(std::string_view slot, std::string_view name, double value)
    {
        for (auto& instance : m_Stages)
        {
            for (const size_t index : instance.slots)
            {
                if (m_Desc.slots[index].slot != slot)
                {
                    continue;
                }
                const auto found = std::ranges::find(instance.desc.parameters, name, &PassParameter::name);
                if (found == instance.desc.parameters.end())
                {
                    continue;
                }
                if (!std::isfinite(value) || value < found->minimum || value > found->maximum)
                {
                    return makeError("Parameter {}.{} must be in [{}, {}]", slot, name, found->minimum, found->maximum);
                }
                instance.parameters[size_t(found - instance.desc.parameters.begin())] = value;
                return {};
            }
        }
        return makeError("Render path {} has no parameter {}.{}", m_Desc.name, slot, name);
    }

    std::expected<void, Error>
    RenderPath::prepare(RenderGraph& graph, std::span<const RenderCamera> cameras, std::span<const RenderLight> lights)
    {
        if (m_ViewCount == 0 || cameras.size() != m_ViewCount)
        {
            return makeError("Render path {} prepares one camera per view after addPasses", m_Desc.name);
        }
        if (auto updated = m_Lights->update(lights); !updated)
        {
            return updated;
        }
        if (auto written = m_Scene->writeMaterials(); !written)
        {
            return written;
        }
        for (uint32_t index = 0; index < cameras.size(); ++index)
        {
            auto&       view   = m_Views[index];
            const auto& camera = cameras[index];
            CameraData  data {};
            data.view                   = camera.view;
            data.projection             = camera.projection;
            data.viewProjection         = camera.projection * camera.view;
            data.inverseView            = glm::inverse(camera.view);
            data.inverseProjection      = glm::inverse(camera.projection);
            data.inverseViewProjection  = glm::inverse(data.viewProjection);
            data.previousViewProjection = view.previousViewProjection.value_or(data.viewProjection);
            data.position               = data.inverseView[3];
            data.imageSize              = {float(view.size.width), float(view.size.height)};
            data.nearPlane              = camera.nearPlane;
            data.farPlane               = camera.farPlane;
            view.previousViewProjection = data.viewProjection;
            auto* mapped                = m_Device.core.MapBuffer(view.camera->handle, 0, sizeof(data));
            if (!mapped)
            {
                return makeError("Map the camera constants of render path {}", m_Desc.name);
            }
            std::memcpy(mapped, &data, sizeof(data));
            m_Device.core.UnmapBuffer(view.camera->handle);
        }
        for (auto& instance : m_Stages)
        {
            // A display stage the caller did not add to this graph has nothing to prepare.
            if (!instance.built)
            {
                continue;
            }
            if (instance.desc.scope == StageScope::eFrame)
            {
                if (auto prepared =
                        instance.stage->prepare(graph, {cameras, std::nullopt, lights, *m_Lights, instance.parameters});
                    !prepared)
                {
                    return prepared;
                }
                continue;
            }
            for (uint32_t view = 0; view < cameras.size(); ++view)
            {
                if (auto prepared =
                        instance.stage->prepare(graph, {cameras, view, lights, *m_Lights, instance.parameters});
                    !prepared)
                {
                    return prepared;
                }
            }
        }
        return {};
    }

    void RenderPath::pollShaders()
    {
        for (auto& instance : m_Stages)
        {
            instance.stage->pollShaders();
        }
    }
} // namespace vultra
