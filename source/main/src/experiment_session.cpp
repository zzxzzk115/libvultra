#include <vultra/main/experiment_session.hpp>
#include <vultra/scene/camera/orbit_camera.hpp>
#include <vultra/scene/scene_import.hpp>
#include <vultra/scene/scene_render_state.hpp>
#include <vultra/servers/rendering/builtin/reference_path_tracer.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <utility>

namespace vultra
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        double elapsedMs(Clock::time_point from, Clock::time_point to)
        {
            return std::chrono::duration<double, std::milli>(to - from).count();
        }

        struct SessionGraph
        {
            // Graph callbacks borrow pass and renderer state; destroy the graph before either owner.
            std::unique_ptr<ReferencePathTracer> reference;
            ReferencePathTracer::Outputs         aovs {};
            GraphBuild                           passes;
            std::unique_ptr<RenderGraph>         graph;
            BuiltinRenderer::Outputs             outputs;
            std::vector<std::string>             marked;
        };
    } // namespace

    struct ExperimentSession::Impl
    {
        Impl(Device& owner, const ExperimentConfig& settings) :
            device(owner),
            config(settings),
            server(device),
            environment(device, initialEnvironment()),
            catalog(device),
            frame(device),
            profiler(device)
        {
            std::vector<SceneMeshInstance> ranges;
            auto imported           = manifest ? importScene(*tree, *manifest, root, config.importOptions, &ranges) :
                                                 importAsset(config.input, config.importOptions);
            cache                   = imported.cachePath;
            gpuScene                = server.uploadScene(imported);
            instances               = std::move(ranges);
            renderer                = std::make_unique<BuiltinRenderer>(device, *gpuScene, environment);
            renderer->settings.path = config.path;
            fallback.center         = gpuScene->center;
            fallback.radius         = gpuScene->radius;
            fallback.distance       = gpuScene->radius * 2.5f;
            fallback.pitch          = 0.2f;
        }

        std::filesystem::path initialEnvironment()
        {
            if (config.input.empty() || config.size.empty())
            {
                throw std::invalid_argument("Experiment requires an input and nonzero dimensions");
            }
            if (config.path == RenderPath::eReferencePathTracing &&
                (!device.core.GetDeviceDesc(device.handle)->hasRayQuery ||
                 !device.core.GetDeviceDesc(device.handle)->hasBindless))
            {
                throw std::invalid_argument(
                    "Reference experiments require a device created with VRI ray query and bindless features");
            }
            config.input = std::filesystem::absolute(config.input);
            if (config.input.extension() == ".vproject")
            {
                root     = config.input.parent_path();
                manifest = ProjectManifest::load(config.input);
                tree     = SceneTree::load(root / manifest->mainScene);
                tree->validateAssets(*manifest);
            }
            if (!config.environment.empty())
            {
                config.environment = std::filesystem::absolute(config.environment);
                return config.environment;
            }
            return manifest ? sceneEnvironmentPath(*tree, *manifest, root) : std::filesystem::path {};
        }

        SessionGraph
        buildGraph(BuiltinRenderer& target, GpuScene& geometry, const std::optional<GraphDefinition>& graphDefinition)
        {
            SessionGraph candidate;
            candidate.graph = std::make_unique<RenderGraph>(device);
            std::vector<GraphBinding> imports;
            if (config.path == RenderPath::eReferencePathTracing)
            {
                candidate.reference    = std::make_unique<ReferencePathTracer>(device, geometry, environment);
                candidate.aovs         = candidate.reference->addPasses(*candidate.graph, config.size);
                candidate.outputs.hdr  = candidate.aovs.hdr;
                candidate.outputs.path = config.path;
                imports                = {{"scene.radiance", candidate.aovs.radiance},
                                          {"scene.albedo", candidate.aovs.albedo},
                                          {"scene.normal", candidate.aovs.normal},
                                          {"scene.depth", candidate.aovs.depth},
                                          {"scene.motion", candidate.aovs.motion},
                                          {"scene.sample_count", candidate.aovs.sampleCount},
                                          {"scene.ray_count", candidate.aovs.rayCount}};
            }
            else
            {
                candidate.outputs = target.addScenePasses(*candidate.graph, config.size);
            }
            imports.insert(imports.begin(), {"scene.hdr", candidate.outputs.hdr});
            if (graphDefinition)
            {
                candidate.passes = graphDefinition->build(*candidate.graph, catalog, imports);
                if (candidate.passes.outputs.empty())
                {
                    throw std::invalid_argument("Experiment graph requires a marked output");
                }
                const auto color = candidate.passes.outputs.front().resource;
                const auto info  = candidate.graph->resourceInfo(color);
                if (!info.isTexture ||
                    (info.textureDesc.format != VriFormat_RGBA16_SFLOAT &&
                     info.textureDesc.format != VriFormat_RGBA8_UNORM) ||
                    info.textureDesc.width != config.size.width || info.textureDesc.height != config.size.height)
                {
                    throw std::invalid_argument(
                        "First experiment graph output must be HDR or display RGBA8 at the scene extent");
                }
                if (info.textureDesc.format == VriFormat_RGBA16_SFLOAT)
                {
                    candidate.outputs.hdr = color;
                }
                else
                {
                    candidate.outputs.color = color;
                }
                for (const auto& entry : candidate.passes.outputs)
                {
                    candidate.marked.push_back(entry.name);
                }
            }
            if (candidate.reference)
            {
                for (const auto& output : imports)
                {
                    if (output.name != "scene.hdr" &&
                        std::ranges::find(candidate.passes.outputs, output.name, &GraphBinding::name) ==
                            candidate.passes.outputs.end())
                    {
                        candidate.graph->exportResource(output.resource);
                        candidate.passes.outputs.push_back(output);
                        candidate.marked.push_back(output.name);
                    }
                }
            }
            if (!candidate.outputs.color.graph)
            {
                candidate.outputs.color =
                    target.addToneMappingPass(*candidate.graph, candidate.outputs.hdr, config.size);
            }
            candidate.graph->exportResource(candidate.outputs.color);
            candidate.graph->exportResource(candidate.outputs.hdr);
            candidate.graph->compile();
            return candidate;
        }

        void replaceGraph(SessionGraph candidate)
        {
            active.graph.reset();
            active   = std::move(candidate);
            rendered = false;
        }

        void syncScene()
        {
            if (!tree)
            {
                return;
            }
            if (config.environment.empty() && gpuSync.environmentChanged(*tree))
            {
                environment.setSource(sceneEnvironmentPath(*tree, *manifest, root));
            }
            if (gpuSync.needsImport(*tree, instances))
            {
                std::vector<SceneMeshInstance> ranges;
                auto imported         = importScene(*tree, *manifest, root, config.importOptions, &ranges);
                auto upload           = server.uploadScene(imported);
                auto replacement      = std::make_unique<BuiltinRenderer>(device, *upload, environment);
                replacement->settings = renderer->settings;
                auto graph            = buildGraph(*replacement, *upload, definition);
                // render() completes the previous frame. Drop its callbacks before retiring their owners.
                active.graph.reset();
                renderer  = std::move(replacement);
                gpuScene  = std::move(upload);
                instances = std::move(ranges);
                cache     = std::move(imported.cachePath);
                gpuSync   = {};
                replaceGraph(std::move(graph));
                server.collectCompletedFrame();
            }
            gpuSync.update(*tree, instances, *gpuScene);
            sceneState.update(*tree, config.size);
        }

        Device&                          device;
        ExperimentConfig                 config;
        std::filesystem::path            root;
        std::filesystem::path            cache;
        std::optional<ProjectManifest>   manifest;
        std::optional<SceneTree>         tree;
        RenderingServer                  server;
        Environment                      environment;
        std::vector<SceneMeshInstance>   instances;
        SceneGpuSync                     gpuSync;
        GpuSceneHandle                   gpuScene;
        std::unique_ptr<BuiltinRenderer> renderer;
        OrbitCamera                      fallback;
        SceneRenderState                 sceneState;
        PassCatalog                      catalog;
        std::optional<GraphDefinition>   definition;
        SessionGraph                     active;
        Frame                            frame;
        Profiler                         profiler;
        RenderCamera                     renderCamera;
        uint64_t                         frameIndex = 0;
        bool                             rendered   = false;
    };

    ExperimentSession::ExperimentSession(Device& device, const ExperimentConfig& config) :
        m_Impl(std::make_unique<Impl>(device, config))
    {
    }

    ExperimentSession::~ExperimentSession() = default;

    SceneTree* ExperimentSession::scene()
    {
        return m_Impl->tree ? &*m_Impl->tree : nullptr;
    }

    const ProjectManifest* ExperimentSession::project() const
    {
        return m_Impl->manifest ? &*m_Impl->manifest : nullptr;
    }

    const std::filesystem::path& ExperimentSession::projectRoot() const
    {
        return m_Impl->root;
    }

    const std::filesystem::path& ExperimentSession::environmentSource() const
    {
        return m_Impl->environment.source();
    }

    const std::filesystem::path& ExperimentSession::cachePath() const
    {
        return m_Impl->cache;
    }

    PassCatalog& ExperimentSession::passes()
    {
        return m_Impl->catalog;
    }

    void ExperimentSession::setGraph(GraphDefinition definition)
    {
        auto candidate = m_Impl->buildGraph(*m_Impl->renderer, *m_Impl->gpuScene, definition);
        m_Impl->replaceGraph(std::move(candidate));
        m_Impl->definition = std::move(definition);
    }

    void ExperimentSession::setPassParameters(std::string_view pass, const PassParameters& parameters)
    {
        auto&      state = *m_Impl;
        const auto found = std::ranges::find(state.active.passes.passes, pass, &BuiltPass::name);
        if (found == state.active.passes.passes.end())
        {
            throw std::invalid_argument("Unknown experiment pass: " + std::string(pass));
        }
        const auto definition = std::ranges::find(state.definition->passes, pass, &GraphPassDesc::id);
        auto       values     = definition->parameters;
        for (const auto& [name, value] : parameters)
        {
            values[name] = value;
        }
        state.catalog.setParameters(*found, values);
        definition->parameters = std::move(values);
    }

    FrameTiming ExperimentSession::render()
    {
        auto&      state = *m_Impl;
        const auto start = Clock::now();
        state.server.collectCompletedFrame();
        state.syncScene();
        if (!state.active.graph)
        {
            state.replaceGraph(state.buildGraph(*state.renderer, *state.gpuScene, state.definition));
        }
        state.renderCamera =
            state.config.camera.value_or(state.sceneState.camera.value_or(state.fallback.camera(state.config.size)));
        auto& graph = *state.active.graph;
        if (state.active.reference)
        {
            const RenderLight fallback {RenderLightKind::eDirectional,
                                        {},
                                        state.renderer->settings.directionToLight,
                                        state.renderer->settings.lightColor,
                                        state.renderer->settings.lightIntensity};
            const auto lighting = state.sceneState.lighting().value_or(std::span<const RenderLight>(&fallback, 1));
            state.active.reference->prepare(state.renderCamera,
                                            graph,
                                            state.active.aovs,
                                            lighting,
                                            state.sceneState.environmentIntensity *
                                                state.renderer->settings.environmentIntensity,
                                            state.config.seed);
            state.renderer->prepareToneMapping(graph, state.active.outputs.hdr);
        }
        else
        {
            state.renderer->prepare(state.renderCamera,
                                    graph,
                                    state.active.outputs,
                                    state.sceneState.lighting(),
                                    state.sceneState.environmentIntensity);
        }
        graph.execute(state.frame.begin(), &state.profiler);
        const auto recorded = Clock::now();
        state.frame.submitAndWait();
        const auto completed = Clock::now();
        if (state.active.reference)
        {
            state.active.reference->completeFrame();
        }
        state.profiler.collect();
        const auto  collected = Clock::now();
        FrameTiming timing;
        timing.frameIndex      = state.frameIndex++;
        timing.prepareRecordMs = elapsedMs(start, recorded);
        timing.submitWaitMs    = elapsedMs(recorded, completed);
        timing.postRenderMs    = elapsedMs(completed, collected);
        timing.totalMs         = elapsedMs(start, collected);
        if (state.profiler.hasGpuTimings())
        {
            double gpuMs = 0;
            for (const auto& pass : state.profiler.timings())
            {
                if (pass.parent == UINT32_MAX)
                {
                    gpuMs += pass.gpuMs;
                }
            }
            timing.gpuMs = gpuMs;
        }
        state.rendered = true;
        return timing;
    }

    const RenderGraph& ExperimentSession::graph() const
    {
        if (!m_Impl->active.graph)
        {
            throw std::logic_error("Experiment has no compiled graph");
        }
        return *m_Impl->active.graph;
    }

    std::span<const PassTiming> ExperimentSession::timings() const
    {
        return m_Impl->profiler.timings();
    }

    const RenderCamera& ExperimentSession::camera() const
    {
        if (!m_Impl->rendered)
        {
            throw std::logic_error("Experiment has no completed frame");
        }
        return m_Impl->renderCamera;
    }

    std::span<const std::string> ExperimentSession::markedOutputs() const
    {
        return m_Impl->active.marked;
    }

    RenderGraph::Resource ExperimentSession::outputResource(std::string_view name) const
    {
        auto& state = *m_Impl;
        if (!state.rendered)
        {
            throw std::logic_error("Experiment has no completed frame");
        }
        if (name == "final")
        {
            return state.active.outputs.color;
        }
        if (name == "hdr")
        {
            return state.active.outputs.hdr;
        }
        const auto found = std::ranges::find(state.active.passes.outputs, name, &GraphBinding::name);
        if (found == state.active.passes.outputs.end())
        {
            throw std::invalid_argument("Unknown experiment output: " + std::string(name));
        }
        return found->resource;
    }

    Texture& ExperimentSession::output(std::string_view name)
    {
        return m_Impl->active.graph->getTexture(outputResource(name));
    }

    const ExperimentConfig& ExperimentSession::configuration() const
    {
        return m_Impl->config;
    }

    Image ExperimentSession::capture(std::string_view name)
    {
        return readback(m_Impl->device, output(name));
    }
} // namespace vultra
