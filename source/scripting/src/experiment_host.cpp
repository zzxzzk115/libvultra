#include <vultra/assets/vpk_archive.hpp>
#include <vultra/main/experiment_description.hpp>
#include <vultra/main/packaged_resources.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/experiment_host.hpp>
#include <vultra/scripting/script_host.hpp>
#include <vultra/servers/rendering/research/graph_report.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        // Allocate identity only; sessions and GPU resources belong exclusively to an explicit host.
        std::atomic<uint64_t> nextSession {1};
        constexpr uint64_t    kSessionCounterMask = (uint64_t(1) << 56) - 1;
        constexpr uint64_t    kSessionTag         = uint64_t(2) << 56;

        uint64_t sessionId()
        {
            auto value = nextSession.load(std::memory_order_relaxed);
            for (;;)
            {
                if (value == 0 || value > kSessionCounterMask)
                {
                    throw std::overflow_error("Experiment ID space exhausted");
                }
                if (nextSession.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
                {
                    return kSessionTag | value;
                }
            }
        }

        std::filesystem::path utf8Path(std::string_view text)
        {
            if (text.contains('\0'))
            {
                throw std::invalid_argument("Experiment path contains a NUL byte");
            }
            return std::filesystem::path(
                std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        StableId persistentId(std::string_view text)
        {
            const auto id = StableId::parse(text);
            if (!id || !id->valid())
            {
                throw std::invalid_argument("Experiment object requires a persistent UUID");
            }
            return *id;
        }

        Node& experimentNode(SceneTree& scene, std::string_view text)
        {
            auto* node = scene.find(NodeId {persistentId(text)});
            if (!node)
            {
                throw std::invalid_argument("Node does not belong to this experiment: " + std::string(text));
            }
            return *node;
        }

        MaterialResource& experimentMaterial(SceneTree& scene, std::string_view text)
        {
            auto* material = scene.findMaterial(AssetId {persistentId(text)});
            if (!material)
            {
                throw std::invalid_argument("Material does not belong to this experiment: " + std::string(text));
            }
            return *material;
        }

        struct HostedSession
        {
            HostedSession(Device& device, const PassCatalog& catalog, const ExperimentConfig& config, float delta) :
                renderer(device, config),
                timeStep(delta)
            {
                for (const auto& definition : catalog.definitions())
                {
                    // Both catalogs install the same built-ins. Copy only the host's project additions.
                    if (!std::ranges::any_of(renderer.passes().definitions(),
                                             [&](const auto& existing)
                                             {
                                                 return existing.type == definition.type;
                                             }))
                    {
                        renderer.passes().add(definition);
                    }
                }
                if (const auto* project = renderer.project())
                {
                    scripts.emplace(*renderer.scene(), false, project);
                    for (const auto& extension : project->extensions)
                    {
                        scripts->addExtension(renderer.scriptPath(extension));
                    }
                    for (const auto& script : project->scripts)
                    {
                        scripts->add(script, renderer.scriptPath(script.path));
                    }
                }
            }

            // Stop scripts before the session removes its materialized module files.
            ExperimentSession         renderer;
            std::optional<ScriptHost> scripts;
            float                     timeStep;
            uint64_t                  frames = 0;
            std::string               snapshot;
        };
    } // namespace

    struct ExperimentHost::Impl
    {
        Impl(Device&               device,
             const PassCatalog&    catalog,
             std::filesystem::path shaderRoot,
             AssetImportOptions    importOptions) :
            device(device),
            catalog(catalog),
            root(std::filesystem::absolute(shaderRoot)),
            importOptions(std::move(importOptions))
        {
        }

        HostedSession& session(uint64_t id) const
        {
            const auto found = sessions.find(id);
            if (found == sessions.end())
            {
                throw std::invalid_argument("Invalid experiment session for this host");
            }
            return *found->second;
        }

        SceneTree& scene(uint64_t id) const
        {
            auto* tree = session(id).renderer.scene();
            if (!tree)
            {
                throw std::invalid_argument("Scene editing requires a project scene, not a direct model input");
            }
            return *tree;
        }

        Device&                                            device;
        const PassCatalog&                                 catalog;
        std::filesystem::path                              root;
        AssetImportOptions                                 importOptions;
        std::map<uint64_t, std::unique_ptr<HostedSession>> sessions;
        std::array<char, 1024>                             error {};
    };

    ExperimentHost::ExperimentHost(Device&               device,
                                   const PassCatalog&    catalog,
                                   std::filesystem::path shaderRoot,
                                   AssetImportOptions    importOptions) :
        m_Impl(std::make_unique<Impl>(device, catalog, std::move(shaderRoot), std::move(importOptions)))
    {
    }

    ExperimentHost::~ExperimentHost() = default;

    uint64_t ExperimentHost::open(std::string_view input,
                                  uint32_t         width,
                                  uint32_t         height,
                                  uint32_t         path,
                                  float            timeStep,
                                  std::string_view environment,
                                  uint32_t         seed)
    {
        if (input.empty() || width == 0 || height == 0 || !std::isfinite(timeStep) || timeStep <= 0 || path > 2)
        {
            throw std::invalid_argument(
                "Experiment requires input, dimensions, positive time step and a valid render path");
        }
        ExperimentConfig config {.input = std::filesystem::absolute(utf8Path(input)),
                                 .size  = {width, height},
                                 .path  = static_cast<RenderPath>(path)};
        if (!environment.empty())
        {
            config.environment = std::filesystem::absolute(utf8Path(environment));
        }
        config.importOptions = m_Impl->importOptions;
        config.seed          = seed;
        ScopedWorkingDirectory cwd(m_Impl->root);
        auto       session = std::make_unique<HostedSession>(m_Impl->device, m_Impl->catalog, config, timeStep);
        const auto id      = sessionId();
        m_Impl->sessions.emplace(id, std::move(session));
        return id;
    }

    uint64_t ExperimentHost::runDescription(std::string_view file)
    {
        auto description                 = ExperimentDescription::load(std::filesystem::absolute(utf8Path(file)));
        description.config.importOptions = m_Impl->importOptions;
        ScopedWorkingDirectory cwd(m_Impl->root);
        auto                   state =
            std::make_unique<HostedSession>(m_Impl->device, m_Impl->catalog, description.config, description.timeStep);
        if (description.graph)
        {
            state->renderer.setGraph(std::move(*description.graph));
        }
        const auto id = sessionId();
        m_Impl->sessions.emplace(id, std::move(state));
        try
        {
            step(id, description.warmup + description.frames);
        }
        catch (...)
        {
            m_Impl->sessions.erase(id);
            throw;
        }
        return id;
    }

    void ExperimentHost::close(uint64_t session)
    {
        m_Impl->session(session);
        m_Impl->sessions.erase(session);
    }

    void ExperimentHost::step(uint64_t session, uint64_t frames)
    {
        auto& state = m_Impl->session(session);
        if (frames == 0 || frames > UINT64_MAX - state.frames)
        {
            throw std::invalid_argument("Experiment step requires a positive non-overflowing frame count");
        }
        ScopedWorkingDirectory cwd(m_Impl->root);
        for (uint64_t i = 0; i < frames; ++i)
        {
            if (state.scripts)
            {
                state.scripts->update(state.timeStep);
            }
            state.renderer.render();
            ++state.frames;
        }
    }

    void ExperimentHost::setGraph(uint64_t session, std::string_view definition)
    {
        auto                   parsed = GraphDefinition::parse(definition);
        ScopedWorkingDirectory cwd(m_Impl->root);
        m_Impl->session(session).renderer.setGraph(std::move(parsed));
    }

    void ExperimentHost::setParameter(uint64_t session, std::string_view pass, std::string_view parameter, double value)
    {
        m_Impl->session(session).renderer.setPassParameters(pass, {{std::string(parameter), value}});
    }

    ExperimentImageInfo ExperimentHost::imageInfo(uint64_t session, std::string_view output)
    {
        const auto& texture = m_Impl->session(session).renderer.output(output);
        return {texture.desc.width, texture.desc.height, uint64_t(texture.desc.width) * texture.desc.height * 4};
    }

    void ExperimentHost::readImage(uint64_t session, std::string_view output, std::span<float> pixels)
    {
        const auto info = imageInfo(session, output);
        if (pixels.size() != info.floatCount)
        {
            throw std::invalid_argument("Experiment readback buffer must contain exactly width * height * 4 floats");
        }
        const auto image = m_Impl->session(session).renderer.capture(output);
        std::ranges::copy(image.rgba, pixels.begin());
    }

    void ExperimentHost::readPreview(uint64_t         session,
                                     std::string_view output,
                                     uint32_t         channel,
                                     float            minimum,
                                     float            maximum,
                                     std::span<float> pixels)
    {
        const auto info = imageInfo(session, output);
        if (pixels.size() != info.floatCount)
        {
            throw std::invalid_argument("Preview readback requires width * height * 4 floats");
        }
        const ImageView view {ImageChannel(channel), minimum, maximum};
        validateImageView(view);
        const auto image = mapImage(m_Impl->session(session).renderer.capture(output), view);
        std::ranges::copy(image.rgba, pixels.begin());
    }

    ExperimentPixel ExperimentHost::probePixel(uint64_t session, std::string_view output, uint32_t x, uint32_t y)
    {
        const auto info = imageInfo(session, output);
        if (x >= info.width || y >= info.height)
        {
            throw std::invalid_argument("Pixel probe is outside the output");
        }
        const auto pixel = imagePixel(m_Impl->session(session).renderer.capture(output), x, y);
        return {pixel[0], pixel[1], pixel[2], pixel[3]};
    }

    ExperimentProgress ExperimentHost::progress(uint64_t session) const
    {
        const auto& state = m_Impl->session(session);
        return {state.frames, double(state.frames) * state.timeStep, state.timeStep};
    }

    std::string_view ExperimentHost::sceneSnapshot(uint64_t session)
    {
        auto& state = m_Impl->session(session);
        if (!state.renderer.scene())
        {
            throw std::invalid_argument("A direct model experiment has no SceneTree snapshot");
        }
        state.snapshot = state.renderer.scene()->serialize();
        return state.snapshot;
    }

    std::string_view ExperimentHost::reportSnapshot(uint64_t session)
    {
        auto& state    = m_Impl->session(session);
        auto& renderer = state.renderer;
        using Json     = nlohmann::json;
        std::vector<GraphCapture> captures {{"final", "", renderer.outputResource("final")},
                                            {"hdr", "", renderer.outputResource("hdr")}};
        for (const auto& output : renderer.markedOutputs())
        {
            captures.push_back({output, "", renderer.outputResource(output)});
        }
        auto        report         = Json::parse(graphReport(renderer.graph(), renderer.timings(), captures));
        const auto& config         = renderer.configuration();
        const auto& device         = *m_Impl->device.core.GetDeviceDesc(m_Impl->device.handle);
        report["experiment"]       = {{"frames", state.frames},
                                      {"seconds", double(state.frames) * state.timeStep},
                                      {"time_step", state.timeStep},
                                      {"seed", config.seed},
                                      {"path", uint32_t(config.path)},
                                      {"width", config.size.width},
                                      {"height", config.size.height}};
        report["provenance"]       = Json::parse(renderer.provenance(m_Impl->root));
        report["device"]           = {{"adapter", device.adapter.name},
                                      {"graphics_api", int(device.graphicsAPI)},
                                      {"enabled_features", device.enabledFeatures},
                                      {"gpu_timestamps", device.hasTimestampQueries}};
        report["outputs"]          = Json::object();
        report["outputs"]["final"] = renderer.outputResource("final").index;
        report["outputs"]["hdr"]   = renderer.outputResource("hdr").index;
        for (const auto& output : renderer.markedOutputs())
        {
            report["outputs"][output] = renderer.outputResource(output).index;
        }
        state.snapshot = report.dump(2) + '\n';
        return state.snapshot;
    }

    CameraSettings ExperimentHost::cameraSettings(uint64_t session, std::string_view node)
    {
        auto& scene = m_Impl->scene(session);
        return sceneCameraSettings(scene, experimentNode(scene, node).id());
    }

    void ExperimentHost::setCameraSettings(uint64_t session, std::string_view node, CameraSettings values)
    {
        auto& scene = m_Impl->scene(session);
        sceneSetCameraSettings(scene, experimentNode(scene, node).id(), values);
    }

    LightSettings ExperimentHost::lightSettings(uint64_t session, std::string_view node)
    {
        auto& scene = m_Impl->scene(session);
        return sceneLightSettings(scene, experimentNode(scene, node).id());
    }

    void ExperimentHost::setLightSettings(uint64_t session, std::string_view node, LightSettings values)
    {
        auto& scene = m_Impl->scene(session);
        sceneSetLightSettings(scene, experimentNode(scene, node).id(), values);
    }

    EnvironmentSettings ExperimentHost::environmentSettings(uint64_t session, std::string_view node)
    {
        auto& scene = m_Impl->scene(session);
        return sceneEnvironmentSettings(scene, experimentNode(scene, node).id());
    }

    void ExperimentHost::setEnvironmentSettings(uint64_t session, std::string_view node, EnvironmentSettings values)
    {
        auto& scene = m_Impl->scene(session);
        sceneSetEnvironmentSettings(scene, experimentNode(scene, node).id(), values);
    }

    MaterialParameters ExperimentHost::materialParameters(uint64_t session, std::string_view material)
    {
        auto& scene = m_Impl->scene(session);
        return sceneMaterialParameters(scene, experimentMaterial(scene, material).id());
    }

    void ExperimentHost::setMaterialParameters(uint64_t session, std::string_view material, MaterialParameters values)
    {
        auto& scene = m_Impl->scene(session);
        sceneSetMaterialParameters(scene, experimentMaterial(scene, material).id(), values);
    }

    void ExperimentHost::readTransform(uint64_t session, std::string_view node, std::span<float> values)
    {
        if (values.size() != 16)
        {
            throw std::invalid_argument("Transform readback requires 16 floats");
        }
        const auto& matrix = experimentNode(m_Impl->scene(session), node).localTransform();
        std::memcpy(values.data(), &matrix, sizeof(matrix));
    }

    void ExperimentHost::setTransform(uint64_t session, std::string_view node, std::span<float> values)
    {
        if (values.size() != 16)
        {
            throw std::invalid_argument("Transform requires 16 floats");
        }
        glm::mat4 matrix;
        std::memcpy(&matrix, values.data(), sizeof(matrix));
        experimentNode(m_Impl->scene(session), node).setLocalTransform(matrix);
    }

    void ExperimentHost::recordError(std::string_view operation, std::string_view message) noexcept
    {
        const auto prefixLength = [](std::string_view text, size_t limit)
        {
            auto length = std::min(text.size(), limit);
            // Truncate borrowed UTF-8 only between code points, so language wrappers can decode it.
            while (length != 0 && length < text.size() && (uint8_t(text[length]) & 0xc0u) == 0x80u)
            {
                --length;
            }
            return int(length);
        };
        std::snprintf(m_Impl->error.data(),
                      m_Impl->error.size(),
                      "%.*s: %.*s",
                      prefixLength(operation, 256),
                      operation.data(),
                      prefixLength(message, 700),
                      message.data());
    }

    std::string_view ExperimentHost::lastError() const
    {
        return m_Impl->error.data();
    }

} // namespace vultra
