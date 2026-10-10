#include <vultra/servers/rendering/research/stereo_renderer.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vultra
{
    struct StereoResearchRenderer::GraphState
    {
        explicit GraphState(Device& device) :
            graph(device)
        {
        }

        std::array<GraphBuild, 2>               methods;
        GraphBuild                              comparison;
        RenderGraph                             graph;
        std::array<Extent, 2>                   sizes;
        std::array<size_t, 2>                   selections;
        RenderPath                              path;
        uint32_t                                shadowResolution;
        uint64_t                                referenceRevision;
        bool                                    sharedMethods;
        bool                                    measurement = false;
        std::optional<ResearchTextureCapture>   capture;
        std::optional<GraphDefinition>          referenceDefinition;
        size_t                                  referenceSelection;
        std::array<BuiltinRenderer::Outputs, 3> scene;
        std::array<StereoOutputs, 2>            hdr;
        std::array<StereoOutputs, 2>            linear;
        std::array<StereoOutputs, 2>            display;
        StereoOutputs                           difference;
        StereoOutputs                           differenceDisplay;
    };

    StereoResearchRenderer::StereoResearchRenderer(Device&                      device,
                                                   const ImportedAsset&         asset,
                                                   const std::filesystem::path& environment,
                                                   const PassCatalog&           catalog,
                                                   std::vector<GraphDefinition> methods,
                                                   GraphDefinition              comparison) :
        m_Device(device),
        m_Environment(device, environment),
        m_Scene(device, asset),
        m_Catalog(catalog),
        m_Methods(std::move(methods)),
        m_Comparison(std::move(comparison))
    {
        settings.path         = RenderPath::eNaiveForward;
        settings.cacheShadows = true; // Research imports retain static geometry and alpha-texture contents.
        for (auto& renderer : m_Renderers)
        {
            renderer = std::make_unique<BuiltinRenderer>(device, m_Scene, m_Environment);
        }
        for (size_t slot = 0; slot < 4; ++slot)
        {
            m_Linear[slot]  = std::make_unique<ToneMappingPass>(device, VriFormat_RGBA16_SFLOAT);
            m_Display[slot] = std::make_unique<ToneMappingPass>(device);
        }
        for (size_t eye = 0; eye < 2; ++eye)
        {
            m_DifferenceBlit[eye] = std::make_unique<TextureBlit>(device, VriFormat_RGBA8_UNORM);
        }
    }

    StereoResearchRenderer::~StereoResearchRenderer()
    {
        m_Device.waitIdle();
        m_State.reset();
    }

    bool StereoResearchRenderer::configure(std::array<Extent, 2>                  sizes,
                                           std::array<size_t, 2>                  selections,
                                           const std::function<void(Texture&)>&   releasePreview,
                                           const std::array<MethodParameters, 2>* parameters,
                                           bool                                   measurement,
                                           bool                                   referenceSnapshot)
    {
        for (size_t eye = 0; eye < 2; ++eye)
        {
            if (sizes[eye].width < 11 || sizes[eye].height < 11 || sizes[eye].width > 8192 ||
                sizes[eye].height > 8192 || selections[eye] >= m_Methods.size())
            {
                throw std::invalid_argument("Invalid eye extent or method selection");
            }
        }
        const auto requestedCapture = measurement ? std::nullopt : capture;
        if (!parameters && m_State && m_State->capture == requestedCapture && m_State->measurement == measurement &&
            m_State->sizes == sizes && m_State->selections == selections && m_State->path == settings.path &&
            m_State->shadowResolution == settings.shadowResolution && m_State->referenceRevision == m_ReferenceRevision)
        {
            return false;
        }
        auto candidate               = std::make_unique<GraphState>(m_Device);
        candidate->sizes             = sizes;
        candidate->selections        = selections;
        candidate->path              = settings.path;
        candidate->shadowResolution  = settings.shadowResolution;
        candidate->referenceRevision = m_ReferenceRevision;
        candidate->measurement       = measurement;
        candidate->capture           = requestedCapture;
        candidate->sharedMethods     = selections[0] == selections[1] &&
                                   (parameters ? !referenceSnapshot : !m_ReferenceDefinition) &&
                                   (!parameters || (*parameters)[0] == (*parameters)[1]);
        if (!parameters && m_State && !m_State->sharedMethods && this->parameters(0) != this->parameters(1))
        {
            // Resizing or enabling inspection must preserve two live configurations of the same method.
            candidate->sharedMethods = false;
        }
        candidate->referenceDefinition = parameters ? std::nullopt : m_ReferenceDefinition;
        candidate->referenceSelection  = parameters ? selections[0] : m_ReferenceSelection;
        auto& graph                    = candidate->graph;
        for (size_t view = 0; view < 3; ++view)
        {
            m_Renderers[view]->settings = settings;
            candidate->scene[view]      = m_Renderers[view]->addScenePasses(graph, sizes[view ? view - 1 : 0]);
        }
        std::vector<GraphBinding> inputs;
        constexpr std::array      names {"source", "left", "right"};
        constexpr std::array      gbufferNames {"position_metallic",
                                           "normal_roughness",
                                           "albedo_weight",
                                           "emission_occlusion",
                                           "specular",
                                           "geometric_normal_ior",
                                           "coat"};
        for (size_t view = 0; view < 3; ++view)
        {
            const auto& scene = candidate->scene[view];
            graph.nameResource(scene.hdr, std::string(names[view]) + ".hdr");
            graph.nameResource(scene.depth, std::string(names[view]) + ".depth");
            for (size_t index = 0; index < scene.shadows.size(); ++index)
            {
                if (scene.shadows[index].graph)
                {
                    graph.nameResource(scene.shadows[index],
                                       std::string(names[view]) + ".shadow" + std::to_string(index));
                }
            }
            inputs.push_back({std::string(names[view]) + ".hdr", scene.hdr});
            inputs.push_back({std::string(names[view]) + ".depth", scene.depth});
            if (settings.path == RenderPath::eNaiveDeferred)
            {
                for (size_t slot = 0; slot < scene.gbuffer.size(); ++slot)
                {
                    graph.nameResource(scene.gbuffer[slot], std::string(names[view]) + "." + gbufferNames[slot]);
                    inputs.push_back({std::string(names[view]) + "." + gbufferNames[slot], scene.gbuffer[slot]});
                }
            }
        }
        for (size_t method = 0; method < 2; ++method)
        {
            if (method == 1 && candidate->sharedMethods)
            {
                candidate->hdr[1]     = candidate->hdr[0];
                candidate->linear[1]  = candidate->linear[0];
                candidate->display[1] = candidate->display[0];
                continue;
            }
            // Prefix pass IDs to isolate two instances of the same graph definition.
            auto definition = m_Methods[selections[method]];
            if (!parameters && method == 0 && m_ReferenceDefinition)
            {
                if (selections[0] != m_ReferenceSelection)
                {
                    throw std::invalid_argument("Reference configuration selection does not match its snapshot");
                }
                definition = *m_ReferenceDefinition;
            }
            else if (!parameters && m_State)
            {
                // Keep live parameters when resizing or selecting another route with the same stage types.
                const size_t previousMethod = m_State->sharedMethods ? 0 : method;
                const auto&  previous       = m_State->methods[previousMethod].passes;
                for (auto& specification : definition.passes)
                {
                    const auto& parameters = m_Catalog.definition(specification.type).parameters;
                    for (const auto& old : previous)
                    {
                        if (old.type != specification.type)
                        {
                            continue;
                        }
                        const auto instanceName = std::string(previousMethod ? "B_" : "A_") + specification.id;
                        for (size_t index = 0; index < parameters.size(); ++index)
                        {
                            if ((m_State->selections[method] == selections[method] && old.name == instanceName) ||
                                parameters[index].shared)
                            {
                                specification.parameters[parameters[index].name] = old.parameterValues[index];
                            }
                        }
                    }
                }
            }
            if (parameters)
            {
                for (const auto& [id, values] : (*parameters)[method])
                {
                    const auto found = std::ranges::find(definition.passes, id, &GraphPassDesc::id);
                    if (found == definition.passes.end())
                    {
                        throw std::invalid_argument("Unknown method Pass in configuration: " + id);
                    }
                    for (const auto& [name, value] : values)
                    {
                        found->parameters[name] = value;
                    }
                }
            }
            if (method == 0 && parameters && referenceSnapshot)
            {
                candidate->referenceDefinition = definition;
            }
            for (const auto& pass : definition.passes)
            {
                if (std::ranges::find(names, pass.id) != names.end())
                {
                    throw std::invalid_argument("Method Pass ID collides with view import namespace: " + pass.id);
                }
            }
            const std::string prefix   = method ? "B_" : "A_";
            auto              endpoint = [&](const std::string& value)
            {
                const auto separator = value.find('.');
                const auto id        = value.substr(0, separator);
                if (std::ranges::any_of(definition.passes,
                                        [&](const auto& pass)
                                        {
                                            return pass.id == id;
                                        }))
                {
                    return prefix + value;
                }
                return value;
            };
            for (auto& edge : definition.edges)
            {
                edge.from = endpoint(edge.from);
                edge.to   = endpoint(edge.to);
            }
            for (auto& output : definition.outputs)
            {
                output = endpoint(output);
            }
            for (auto& pass : definition.passes)
            {
                pass.id = prefix + pass.id;
            }
            candidate->methods[method] = definition.build(graph, m_Catalog, inputs);
            const auto& outputs        = candidate->methods[method].outputs;
            if (outputs.size() != 2)
            {
                throw std::invalid_argument("A research method must export left/right images");
            }
            candidate->hdr[method] = {outputs[0].resource, outputs[1].resource};
            for (size_t eye = 0; eye < 2; ++eye)
            {
                const auto hdr  = candidate->hdr[method][eye];
                const auto info = graph.resourceInfo(hdr);
                if (!info.isTexture ||
                    (info.textureDesc.format != VriFormat_RGBA16_SFLOAT &&
                     info.textureDesc.format != VriFormat_RGBA32_SFLOAT) ||
                    info.textureDesc.width != sizes[eye].width || info.textureDesc.height != sizes[eye].height)
                {
                    throw std::invalid_argument("A method must return one linear RGBA16F or RGBA32F texture per eye");
                }
                graph.exportResource(hdr);
                if (measurement)
                {
                    continue;
                }
                const size_t      slot = method * 2 + eye;
                const std::string name = std::string(method ? "B" : "A") + (eye ? ".right" : ".left");
                const std::array  source {hdr};
                candidate->linear[method][eye] =
                    m_Linear[slot]->addPasses(graph, name + ".linear_display", source, m_ToneParameters).front();
                candidate->display[method][eye] =
                    m_Display[slot]->addPasses(graph, name + ".display", source, m_ToneParameters).front();
                graph.exportResource(candidate->linear[method][eye]);
                graph.exportResource(candidate->display[method][eye]);
            }
        }
        if (!measurement)
        {
            const std::array comparisonInputs {GraphBinding {"a.left", candidate->hdr[0][0]},
                                               GraphBinding {"a.right", candidate->hdr[0][1]},
                                               GraphBinding {"b.left", candidate->hdr[1][0]},
                                               GraphBinding {"b.right", candidate->hdr[1][1]}};
            candidate->comparison = m_Comparison.build(graph, m_Catalog, comparisonInputs);
            if (candidate->comparison.outputs.size() != 2)
            {
                throw std::invalid_argument("Comparison graph must export left/right HDR differences");
            }
            for (size_t eye = 0; eye < 2; ++eye)
            {
                const std::string name       = eye ? "Difference.right" : "Difference.left";
                const auto        difference = candidate->comparison.outputs[eye].resource;
                const auto        info       = graph.resourceInfo(difference);
                if (!info.isTexture || info.textureDesc.format != VriFormat_RGBA32_SFLOAT ||
                    info.textureDesc.width != sizes[eye].width || info.textureDesc.height != sizes[eye].height)
                {
                    throw std::invalid_argument("Comparison must produce matching linear RGBA32F images");
                }
                const auto display                = graph.createTexture(name + ".display", colorTexture(sizes[eye]));
                candidate->difference[eye]        = difference;
                candidate->differenceDisplay[eye] = display;
                graph.exportResource(difference);
                graph.exportResource(display);
                graph.addPass(name + ".preview",
                              {{difference, Usage::eSampled}, {display, Usage::eColorWrite}},
                              [this, eye, difference, display, size = sizes[eye]](auto* cmd, auto& current)
                              {
                                  auto& blit = *m_DifferenceBlit[eye];
                                  blit.setSource(0, current.getTexture(difference));
                                  blit.draw(cmd,
                                            current.getTexture(display),
                                            {0, 0, size.width, size.height},
                                            0,
                                            true,
                                            ImageView {ImageChannel::eRgb, 0, 1 / differenceGain});
                              });
            }
            std::vector<RenderGraph::Use> previews;
            for (size_t method = 0; method < 2; ++method)
            {
                if (method == 1 && candidate->sharedMethods)
                {
                    continue;
                }
                for (const auto resource : candidate->display[method])
                {
                    previews.push_back({resource, Usage::eSampled});
                }
            }
            for (const auto resource : candidate->differenceDisplay)
            {
                previews.push_back({resource, Usage::eSampled});
            }
            graph.addPass(
                "UI previews",
                std::span(previews),
                [](auto*, auto&)
                {
                },
                true);
        }
        if (requestedCapture)
        {
            auto bindings = inputs;
            for (uint32_t method = 0; method < 2; ++method)
            {
                const auto actual = method == 1 && candidate->sharedMethods ? 0 : method;
                for (const auto& pass : candidate->methods[actual].passes)
                {
                    const auto& ports = m_Catalog.definition(pass.type).outputs;
                    for (size_t port = 0; port < ports.size(); ++port)
                    {
                        auto name = pass.name + "." + ports[port].name;
                        if (method != actual)
                        {
                            name[0] = 'B';
                        }
                        bindings.push_back({name, pass.outputs[port]});
                    }
                }
            }
            const auto found        = std::ranges::find(bindings, requestedCapture->endpoint, &GraphBinding::name);
            auto       resourceName = requestedCapture->endpoint;
            if (candidate->sharedMethods && resourceName.starts_with("B_"))
            {
                resourceName[0] = 'A';
            }
            const auto resource  = found == bindings.end() ? graph.findResource(resourceName) : found->resource;
            auto       afterPass = requestedCapture->afterPass;
            if (candidate->sharedMethods && afterPass.starts_with("B_"))
            {
                afterPass[0] = 'A';
            }
            graph.captureAfterPass(afterPass, resource, "Inspection.snapshot");
        }
        graph.compile();
        for (auto& renderer : m_Renderers)
        {
            renderer->invalidateShadowCache();
        }
        if (releasePreview && m_State)
        {
            visitDisplays(releasePreview);
        }
        m_ReferenceDefinition = candidate->referenceDefinition;
        m_ReferenceSelection  = candidate->referenceSelection;
        m_State               = std::move(candidate);
        return true;
    }

    std::array<std::array<uint32_t, 5>, 3> StereoResearchRenderer::primitiveCounts() const
    {
        std::array<std::array<uint32_t, 5>, 3> result {};
        if (m_State)
        {
            for (size_t view = 0; view < 3; ++view)
            {
                if (m_State->graph.resourceInfo(m_State->scene[view].hdr).active)
                {
                    result[view] = m_Renderers[view]->primitiveCounts();
                }
            }
        }
        return result;
    }

    void StereoResearchRenderer::prepare(const StereoFrameViews& views)
    {
        if (!m_State)
        {
            throw std::logic_error("Configure the renderer before preparing a frame");
        }
        if (m_State->path != settings.path || m_State->shadowResolution != settings.shadowResolution)
        {
            throw std::logic_error("Reconfigure after changing the render path or shadow resolution");
        }
        if (!std::isfinite(differenceGain) || differenceGain <= 0)
        {
            throw std::invalid_argument("Difference gain must be finite and positive");
        }
        m_Views             = views;
        m_ToneParameters[0] = settings.exposure;
        m_ToneParameters[1] = settings.meshShading && settings.meshletColors ? 1 : 0;
        m_ToneParameters[2] = double(settings.toneOperator);
        for (size_t view = 0; view < 3; ++view)
        {
            if (m_State->graph.resourceInfo(m_State->scene[view].hdr).active)
            {
                m_Renderers[view]->settings = settings;
                m_Renderers[view]->prepare(m_Views.cameras[view], m_State->graph, m_State->scene[view]);
            }
        }
    }

    void StereoResearchRenderer::record(VriCommandBuffer* cmd, Profiler* profiler)
    {
        if (!m_State)
        {
            throw std::logic_error("Configure the renderer before recording a frame");
        }
        m_State->graph.execute(cmd, profiler);
    }

    void StereoResearchRenderer::completeFrame()
    {
        for (auto& renderer : m_Renderers)
        {
            renderer->completeFrame();
        }
    }

    void StereoResearchRenderer::poll()
    {
        for (auto& renderer : m_Renderers)
        {
            renderer->pollShaders();
        }
        for (size_t slot = 0; slot < 4; ++slot)
        {
            m_Linear[slot]->shader().poll();
            m_Display[slot]->shader().poll();
        }
    }

    bool StereoResearchRenderer::ready() const
    {
        return bool(m_State);
    }

    void StereoResearchRenderer::captureReference()
    {
        if (!m_State)
        {
            throw std::logic_error("A rendered configuration is required before capturing a reference");
        }
        auto        definition = m_Methods[m_State->selections[1]];
        const auto& current    = m_State->sharedMethods ? m_State->methods[0].passes : m_State->methods[1].passes;
        for (auto& specification : definition.passes)
        {
            const auto name  = std::string(m_State->sharedMethods ? "A_" : "B_") + specification.id;
            const auto found = std::ranges::find(current, name, &BuiltPass::name);
            if (found == current.end())
            {
                throw std::logic_error("Missing active pass when capturing a reference");
            }
            const auto& parameters = m_Catalog.definition(specification.type).parameters;
            for (size_t index = 0; index < parameters.size(); ++index)
            {
                specification.parameters[parameters[index].name] = found->parameterValues[index];
            }
        }
        m_ReferenceDefinition = std::move(definition);
        m_ReferenceSelection  = m_State->selections[1];
        ++m_ReferenceRevision;
    }

    void StereoResearchRenderer::useRenderedReference()
    {
        m_ReferenceDefinition.reset();
        ++m_ReferenceRevision;
    }

    bool StereoResearchRenderer::referenceCaptured() const
    {
        return m_State && m_State->referenceDefinition.has_value();
    }

    void StereoResearchRenderer::discardPendingReference()
    {
        if (m_State)
        {
            m_ReferenceDefinition = m_State->referenceDefinition;
            m_ReferenceSelection  = m_State->referenceSelection;
            m_ReferenceRevision   = m_State->referenceRevision;
            capture               = m_State->capture;
        }
    }

    bool StereoResearchRenderer::methodsShared() const
    {
        return m_State && m_State->sharedMethods;
    }

    bool StereoResearchRenderer::sourceActive() const
    {
        return m_State && m_State->graph.resourceInfo(m_State->scene[0].hdr).active;
    }

    Texture& StereoResearchRenderer::texture(StereoOutput output, uint32_t method, uint32_t eye)
    {
        if (!m_State || method > 1 || eye > 1)
        {
            throw std::out_of_range("StereoResearchRenderer output selection");
        }
        switch (output)
        {
            case StereoOutput::eLinearHdr:
                return m_State->graph.getTexture(m_State->hdr[method][eye]);
            case StereoOutput::eLinearDisplay:
                return m_State->graph.getTexture(m_State->linear[method][eye]);
            case StereoOutput::eDisplay:
                return m_State->graph.getTexture(m_State->display[method][eye]);
            case StereoOutput::eDifferenceHdr:
                return m_State->graph.getTexture(m_State->difference[eye]);
            case StereoOutput::eDifferenceDisplay:
                return m_State->graph.getTexture(m_State->differenceDisplay[eye]);
        }
        throw std::invalid_argument("Unknown renderer output");
    }

    void StereoResearchRenderer::visitDisplays(const std::function<void(Texture&)>& visit)
    {
        if (!m_State || m_State->measurement)
        {
            return;
        }
        std::vector<VriTexture*> visited;
        for (const auto output : {StereoOutput::eDisplay, StereoOutput::eDifferenceDisplay})
        {
            for (uint32_t method = 0; method < 2; ++method)
            {
                for (uint32_t eye = 0; eye < 2; ++eye)
                {
                    auto& image = texture(output, method, eye);
                    if (std::find(visited.begin(), visited.end(), image.handle) == visited.end())
                    {
                        visited.push_back(image.handle);
                        visit(image);
                    }
                }
            }
        }
    }

    std::array<size_t, 2> StereoResearchRenderer::selections() const
    {
        if (!m_State)
        {
            throw std::logic_error("StereoResearchRenderer is not configured");
        }
        return m_State->selections;
    }

    std::span<BuiltPass> StereoResearchRenderer::passes(uint32_t method)
    {
        if (!m_State || method > 1)
        {
            throw std::out_of_range("Research method");
        }
        if (method == 1 && m_State->sharedMethods)
        {
            method = 0;
        }
        return m_State->methods[method].passes;
    }

    const StereoFrameViews& StereoResearchRenderer::views() const
    {
        return m_Views;
    }

    const GpuScene& StereoResearchRenderer::scene() const
    {
        return m_Scene;
    }

    MethodParameters StereoResearchRenderer::parameters(uint32_t method)
    {
        MethodParameters result;
        const size_t     actual = m_State && m_State->sharedMethods ? 0 : method;
        for (const auto& pass : passes(method))
        {
            const auto& metadata = m_Catalog.definition(pass.type).parameters;
            auto&       values   = result[pass.name.substr(std::string(actual ? "B_" : "A_").size())];
            for (size_t i = 0; i < metadata.size(); ++i)
            {
                values[metadata[i].name] = pass.parameterValues[i];
            }
        }
        return result;
    }

    RenderGraph::Snapshot StereoResearchRenderer::graphSnapshot() const
    {
        if (!m_State)
        {
            throw std::logic_error("Research renderer is not configured");
        }
        return m_State->graph.snapshot();
    }

    Texture& StereoResearchRenderer::resourceTexture(std::string_view name)
    {
        const auto snapshot = graphSnapshot();
        for (size_t i = 0; i < snapshot.resources.size(); ++i)
        {
            const auto& info = snapshot.resources[i];
            if (info.name == name && info.active && info.isTexture)
            {
                return m_State->graph.getTexture({&m_State->graph, uint32_t(i)});
            }
        }
        throw std::invalid_argument("No active texture named: " + std::string(name));
    }

    std::vector<const void*> StereoResearchRenderer::sceneObjects() const
    {
        std::vector<const void*> result;
        for (const auto* buffer : {m_Scene.vertices.get(), m_Scene.indices.get(), m_Scene.transforms.get()})
        {
            if (buffer)
            {
                result.push_back(buffer->handle);
            }
        }
        if (m_Scene.meshlets)
        {
            for (const auto* buffer : m_Scene.meshlets->buffers())
            {
                result.push_back(buffer);
            }
        }
        for (const auto& texture : m_Scene.textures)
        {
            result.push_back(texture->handle);
        }
        for (const auto* texture : {m_Environment.radiance.get(),
                                    m_Environment.diffuse.get(),
                                    m_Environment.specular.get(),
                                    m_Environment.brdfLut.get()})
        {
            if (texture)
            {
                result.push_back(texture->handle);
            }
        }
        return result;
    }
} // namespace vultra
