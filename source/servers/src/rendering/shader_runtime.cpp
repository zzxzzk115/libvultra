#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
#include "shader_source_reload.hpp"
#endif

#include <vultra/assets/asset_source.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/servers/rendering/shader_runtime.hpp>

#include <atomic>
#include <stdexcept>

namespace vultra
{
    struct ShaderRuntime::State
    {
        struct Material
        {
            MaterialInstance                instance;
            std::unique_ptr<ShaderMaterial> gpu;
        };

        struct Snapshot
        {
            ShaderAsset                            asset;
            std::vector<std::unique_ptr<Material>> materials;
        };

        Device&                             device;
        std::filesystem::path               file;
        const AssetSource*                  assetSource;
        ShaderMaterial::TextureResolver     textures;
        std::vector<std::string>            requiredLightModes;
        ShaderAsset::SubshaderCompatibility compatible;
        std::unique_ptr<Snapshot>           live;
        std::string                         diagnostics;
        uint64_t                            generation = 1;
        std::atomic<uint64_t>               revision {0};
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
        std::unique_ptr<detail::ShaderSourceReload> source;
#endif

        State(Device&                               device,
              std::filesystem::path                 file,
              [[maybe_unused]] ShaderCompileOptions options,
              ShaderMaterial::TextureResolver       textures,
              std::vector<std::string>              requiredLightModes,
              ShaderAsset::SubshaderCompatibility   compatible,
              const AssetSource*                    assetSource) :
            device(device),
            file(assetSource ? assetSource->resolve(file) : std::filesystem::absolute(file)),
            assetSource(assetSource),
            textures(std::move(textures)),
            requiredLightModes(std::move(requiredLightModes)),
            compatible(std::move(compatible))
        {
            if (this->file.extension() != ".vshaderc")
            {
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
                source = std::make_unique<detail::ShaderSourceReload>(this->file, std::move(options), revision);
#else
                throw std::invalid_argument(
                    "ShaderRuntime requires a cooked .vshaderc asset; source needs game_shader");
#endif
            }
            live        = std::make_unique<Snapshot>();
            live->asset = loadAsset();
            diagnostics = live->asset.diagnostics;
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
            if (source)
            {
                source->refreshWatch(live->asset);
            }
#endif
        }

        ~State()
        {
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
            source.reset();
#endif
            device.waitIdleForDestruction();
            live.reset();
        }

        ShaderAsset loadAsset() const
        {
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
            if (source)
            {
                return ShaderAsset::compile(source->file, source->options);
            }
#endif
            return ShaderAsset::load(file, assetSource);
        }

        std::unique_ptr<ShaderMaterial> createMaterial(const ShaderAsset& asset, MaterialInstance& instance)
        {
            std::vector<std::string_view> modes(requiredLightModes.begin(), requiredLightModes.end());
            return std::make_unique<ShaderMaterial>(device, asset, instance, textures, modes, compatible);
        }

        bool publish(ShaderAsset asset, uint64_t sourceRevision, const PrepareCandidate& prepare)
        {
            try
            {
                if (revision.load(std::memory_order_relaxed) != sourceRevision)
                {
                    Logger::core().info("Discarded stale shader candidate revision {}", sourceRevision);
                    return false;
                }
                auto candidate       = std::make_unique<Snapshot>();
                candidate->asset     = std::move(asset);
                std::string messages = candidate->asset.diagnostics;
                device.waitIdle();
                for (uint32_t i = 0; i < live->materials.size(); ++i)
                {
                    if (!live->materials[i])
                    {
                        candidate->materials.push_back(nullptr);
                        continue;
                    }
                    const auto& old = *live->materials[i];
                    if (old.gpu->pipelineCount() && !prepare)
                    {
                        throw std::invalid_argument(
                            "Shader reload requires a candidate preparation callback for live GPU passes");
                    }
                    auto material      = std::make_unique<Material>();
                    material->instance = old.instance;
                    material->instance.reconcile(live->asset, candidate->asset, messages);
                    material->gpu = createMaterial(candidate->asset, material->instance);
                    if (prepare)
                    {
                        prepare(i, *material->gpu);
                    }
                    candidate->materials.push_back(std::move(material));
                }
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
                if (source)
                {
                    if (!source->dependenciesCurrent(candidate->asset))
                    {
                        // Watch delivery can lag a write performed during GPU preparation.
                        revision.fetch_add(1, std::memory_order_relaxed);
                        diagnostics = "Discarded shader candidate: dependency changed during GPU preparation";
                        Logger::core().info("{}", diagnostics);
                        return false;
                    }
                    source->refreshWatch(candidate->asset);
                }
#endif
                if (revision.load(std::memory_order_relaxed) != sourceRevision)
                {
                    Logger::core().info("Discarded shader candidate changed during GPU preparation");
                    return false;
                }
                live.swap(candidate);
                diagnostics = std::move(messages);
                ++generation;
                Logger::core().info("Published shader {} generation {} with {} materials",
                                    live->asset.name,
                                    generation,
                                    live->materials.size());
                return true;
            }
            catch (const std::exception& error)
            {
                diagnostics = error.what();
                Logger::core().warn("Shader candidate rejected; previous asset retained: {}", diagnostics);
                return false;
            }
        }
    };

    ShaderRuntime::ShaderRuntime(Device&                               device,
                                 std::filesystem::path                 source,
                                 [[maybe_unused]] ShaderCompileOptions options,
                                 ShaderMaterial::TextureResolver       textures,
                                 std::vector<std::string>              requiredLightModes,
                                 ShaderAsset::SubshaderCompatibility   compatible,
                                 const AssetSource*                    assetSource) :
        m_State(std::make_unique<State>(device,
                                        std::move(source),
                                        std::move(options),
                                        std::move(textures),
                                        std::move(requiredLightModes),
                                        std::move(compatible),
                                        assetSource))
    {
    }

    ShaderRuntime::~ShaderRuntime() = default;

    uint32_t ShaderRuntime::addMaterial(MaterialInstance instance)
    {
        auto material      = std::make_unique<State::Material>();
        material->instance = std::move(instance);
        material->gpu      = m_State->createMaterial(m_State->live->asset, material->instance);
        for (uint32_t i = 0; i < m_State->live->materials.size(); ++i)
        {
            if (!m_State->live->materials[i])
            {
                m_State->live->materials[i] = std::move(material);
                return i;
            }
        }
        m_State->live->materials.push_back(std::move(material));
        return uint32_t(m_State->live->materials.size() - 1);
    }

    void ShaderRuntime::removeMaterial(uint32_t index)
    {
        auto& material = m_State->live->materials.at(index);
        if (!material)
        {
            throw std::invalid_argument("Shader material index was removed");
        }
        m_State->device.waitIdle();
        material.reset();
    }

    const ShaderAsset& ShaderRuntime::asset() const
    {
        return m_State->live->asset;
    }

    MaterialInstance& ShaderRuntime::instance(uint32_t index)
    {
        const auto& material = m_State->live->materials.at(index);
        if (!material)
        {
            throw std::invalid_argument("Shader material index was removed");
        }
        return material->instance;
    }

    ShaderMaterial& ShaderRuntime::material(uint32_t index)
    {
        const auto& material = m_State->live->materials.at(index);
        if (!material)
        {
            throw std::invalid_argument("Shader material index was removed");
        }
        return *material->gpu;
    }

    uint64_t ShaderRuntime::generation() const
    {
        return m_State->generation;
    }

    const std::string& ShaderRuntime::diagnostics() const
    {
        return m_State->diagnostics;
    }

#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
    bool ShaderRuntime::compiling() const
    {
        return m_State->source && m_State->source->job != nullptr;
    }
#endif

    bool ShaderRuntime::reload(const PrepareCandidate& prepare)
    {
        auto&                       state    = *m_State;
        [[maybe_unused]] const auto revision = state.revision.fetch_add(1, std::memory_order_relaxed) + 1;
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
        if (state.source)
        {
            state.source->started  = revision;
            state.source->observed = revision;
        }
#endif
        try
        {
            auto candidate = state.loadAsset();
            // Notifications for the write that triggered this synchronous compile may arrive during it.
            // Publication still checks dependency bytes and rejects edits made during GPU preparation.
            const auto compiledRevision = state.revision.load(std::memory_order_relaxed);
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
            if (state.source)
            {
                state.source->started  = compiledRevision;
                state.source->observed = compiledRevision;
            }
#endif
            return state.publish(std::move(candidate), compiledRevision, prepare);
        }
        catch (const std::exception& error)
        {
            state.diagnostics = error.what();
            Logger::core().warn("Shader reload failed; previous asset retained: {}", state.diagnostics);
            return false;
        }
    }

#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
    bool ShaderRuntime::poll(const PrepareCandidate& prepare)
    {
        auto& state = *m_State;
        if (!state.source)
        {
            return false;
        }
        return state.source->poll(
            state.live->asset,
            [&](ShaderAsset asset, uint64_t revision)
            {
                return state.publish(std::move(asset), revision, prepare);
            },
            state.diagnostics);
    }
#endif
} // namespace vultra
