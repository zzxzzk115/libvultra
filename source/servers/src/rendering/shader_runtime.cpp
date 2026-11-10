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
    namespace
    {
        // Only existing throwing callers use these transitional callback adapters.
        ShaderMaterial::TextureResolverExpected legacyTextures(ShaderMaterial::TextureResolver callback)
        {
            if (!callback)
            {
                return {};
            }
            return
                [callback = std::move(callback)](const auto& property,
                                                 const auto& value) -> std::expected<ShaderTextureBinding, std::string>
            {
                try
                {
                    return callback(property, value);
                }
                catch (const std::exception& error)
                {
                    return std::unexpected(error.what());
                }
            };
        }

        ShaderAsset::SubshaderCompatibilityExpected legacyCompatibility(ShaderAsset::SubshaderCompatibility callback)
        {
            if (!callback)
            {
                return {};
            }
            return [callback = std::move(callback)](const auto& subshader) -> std::expected<std::string, std::string>
            {
                try
                {
                    return callback(subshader);
                }
                catch (const std::exception& error)
                {
                    return std::unexpected(error.what());
                }
            };
        }

        ShaderRuntime::PrepareCandidateExpected legacyPreparation(ShaderRuntime::PrepareCandidate callback)
        {
            if (!callback)
            {
                return {};
            }
            return [callback = std::move(callback)](uint32_t        index,
                                                    ShaderMaterial& material) -> std::expected<void, std::string>
            {
                try
                {
                    callback(index, material);
                    return {};
                }
                catch (const std::exception& error)
                {
                    return std::unexpected(error.what());
                }
            };
        }
    } // namespace

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

        Device&                                     device;
        std::filesystem::path                       file;
        const AssetSource*                          assetSource = nullptr;
        ShaderMaterial::TextureResolverExpected     textures;
        std::vector<std::string>                    requiredLightModes;
        ShaderAsset::SubshaderCompatibilityExpected compatible;
        std::unique_ptr<Snapshot>                   live;
        std::string                                 diagnostics;
        uint64_t                                    generation = 1;
        bool                                        publishing = false;
        std::atomic<uint64_t>                       revision {0};
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
        std::unique_ptr<detail::ShaderSourceReload> source;
#endif

        explicit State(Device& value) :
            device(value)
        {
        }

        ~State()
        {
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
            source.reset();
#endif
            if (live && !live->materials.empty())
            {
                device.waitIdleForDestruction();
            }
            live.reset();
        }

        std::expected<void, std::string> initialize(ShaderCompileOptions options)
        {
            if (file.extension() != ".vshaderc")
            {
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
                source = std::make_unique<detail::ShaderSourceReload>(file, std::move(options), revision);
#else
                static_cast<void>(options);
                return std::unexpected("ShaderRuntime requires a cooked .vshaderc asset; source needs game_shader");
#endif
            }
            auto asset = loadAssetExpected();
            if (!asset)
            {
                return std::unexpected(asset.error());
            }
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
            if (source)
            {
                auto plan = source->prepareAcceptedExpected(*asset);
                if (!plan)
                {
                    return std::unexpected(plan.error());
                }
                source->accept(std::move(*plan));
            }
#endif
            live        = std::make_unique<Snapshot>();
            live->asset = std::move(*asset);
            diagnostics = live->asset.diagnostics;
            return {};
        }

        std::expected<ShaderAsset, std::string> loadAssetExpected() const
        {
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
            if (source)
            {
                return ShaderAsset::compileExpected(source->file, source->options);
            }
#endif
            return ShaderAsset::loadExpected(file, assetSource);
        }

        std::expected<std::unique_ptr<ShaderMaterial>, std::string> createMaterialExpected(const ShaderAsset& asset,
                                                                                           MaterialInstance&  instance)
        {
            std::vector<std::string_view> modes(requiredLightModes.begin(), requiredLightModes.end());
            return ShaderMaterial::create(device, asset, instance, textures, modes, compatible);
        }

        std::unexpected<std::string> reject(std::string message)
        {
            diagnostics = std::move(message);
            Logger::core().warn("Shader candidate rejected; previous asset retained: {}", diagnostics);
            return std::unexpected(diagnostics);
        }

        std::expected<bool, std::string>
        publishExpected(ShaderAsset asset, uint64_t sourceRevision, const PrepareCandidateExpected& prepare)
        {
            if (publishing)
            {
                return reject("ShaderRuntime publication is already in progress");
            }
            if (generation == UINT64_MAX)
            {
                return reject("ShaderRuntime generation exhausted");
            }
            if (revision.load(std::memory_order_relaxed) != sourceRevision)
            {
                return false;
            }
            publishing = true;

            struct Publication
            {
                bool& active;

                ~Publication()
                {
                    active = false;
                }
            } publication {publishing};
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
            if (source)
            {
                if (auto observed = source->observeCandidateExpected(asset); !observed)
                {
                    return reject(observed.error());
                }
            }
#endif
            auto candidate       = std::make_unique<Snapshot>();
            candidate->asset     = std::move(asset);
            std::string messages = candidate->asset.diagnostics;
            if (auto idle = device.waitIdleExpected(); !idle)
            {
                return reject(idle.error());
            }

            struct ObservedInstance
            {
                uint64_t    revision = 0;
                std::string variant;
                AssetId     shader;
            };

            std::vector<ObservedInstance> observed(live->materials.size());
            candidate->materials.resize(live->materials.size());
            // Snapshot every CPU instance before any candidate callback can edit external host state.
            for (size_t index = 0; index < live->materials.size(); ++index)
            {
                if (!live->materials[index])
                {
                    continue;
                }
                const auto& old = *live->materials[index];
                if (old.gpu->pipelineCount() && !prepare)
                {
                    return reject("Shader reload requires a candidate preparation callback for live GPU passes");
                }
                observed[index]             = {old.instance.revision(), old.instance.variant, old.instance.shader};
                auto material               = std::make_unique<Material>();
                material->instance          = old.instance;
                candidate->materials[index] = std::move(material);
            }
            for (uint32_t index = 0; index < candidate->materials.size(); ++index)
            {
                if (!candidate->materials[index])
                {
                    continue;
                }
                auto& material = *candidate->materials[index];
                if (auto reconciled = material.instance.reconcileExpected(live->asset, candidate->asset, messages);
                    !reconciled)
                {
                    return reject(reconciled.error());
                }
                auto gpu = createMaterialExpected(candidate->asset, material.instance);
                if (!gpu)
                {
                    return reject(gpu.error());
                }
                material.gpu = std::move(*gpu);
                if (prepare)
                {
                    if (auto prepared = prepare(index, *material.gpu); !prepared)
                    {
                        return reject("Material " + std::to_string(index) + ": " + prepared.error());
                    }
                }
            }
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
            std::optional<detail::ShaderSourceReload::WatchPlan> watches;
            if (source)
            {
                auto current = source->dependenciesCurrentExpected(candidate->asset);
                if (!current)
                {
                    return reject(current.error());
                }
                if (!*current)
                {
                    revision.fetch_add(1, std::memory_order_relaxed);
                    return reject("Discarded shader candidate: dependency changed during GPU preparation");
                }
                auto prepared = source->prepareAcceptedExpected(candidate->asset);
                if (!prepared)
                {
                    return reject(prepared.error());
                }
                watches.emplace(std::move(*prepared));
            }
#endif
            for (size_t index = 0; index < live->materials.size(); ++index)
            {
                if (!live->materials[index])
                {
                    continue;
                }
                const auto& instance = live->materials[index]->instance;
                if (instance.revision() != observed[index].revision || instance.variant != observed[index].variant ||
                    instance.shader != observed[index].shader)
                {
                    return reject("Discarded shader candidate: live material changed during GPU preparation");
                }
            }
            if (revision.load(std::memory_order_relaxed) != sourceRevision)
            {
                return false;
            }
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
            if (watches)
            {
                source->accept(std::move(*watches));
            }
#endif
            live.swap(candidate);
            diagnostics.swap(messages);
            ++generation;
            Logger::core().info("Published shader {} generation {} with {} materials",
                                live->asset.name,
                                generation,
                                live->materials.size());
            return true;
        }
    };

    ShaderRuntime::ShaderRuntime(std::unique_ptr<State> state) :
        m_State(std::move(state))
    {
    }

    std::expected<std::unique_ptr<ShaderRuntime>, std::string>
    ShaderRuntime::create(Device&                                     device,
                          std::filesystem::path                       file,
                          ShaderCompileOptions                        options,
                          ShaderMaterial::TextureResolverExpected     textures,
                          std::vector<std::string>                    requiredLightModes,
                          ShaderAsset::SubshaderCompatibilityExpected compatible,
                          const AssetSource*                          assetSource)
    {
        if (file.empty() ||
            file.native().find(std::filesystem::path::value_type(0)) != std::filesystem::path::string_type::npos)
        {
            return std::unexpected("ShaderRuntime requires a nonempty path without NUL characters");
        }
        auto state = std::make_unique<State>(device);
        if (assetSource)
        {
            auto resolved = assetSource->resolveExpected(file);
            if (!resolved)
            {
                return std::unexpected(resolved.error());
            }
            state->file = std::move(*resolved);
        }
        else
        {
            std::error_code error;
            state->file = std::filesystem::absolute(file, error).lexically_normal();
            if (error)
            {
                return std::unexpected("Resolve ShaderRuntime path: " + error.message());
            }
        }
        state->assetSource        = assetSource;
        state->textures           = std::move(textures);
        state->requiredLightModes = std::move(requiredLightModes);
        state->compatible         = std::move(compatible);
        if (auto initialized = state->initialize(std::move(options)); !initialized)
        {
            return std::unexpected(initialized.error());
        }
        return std::unique_ptr<ShaderRuntime>(new ShaderRuntime(std::move(state)));
    }

    ShaderRuntime::ShaderRuntime(Device&                             device,
                                 std::filesystem::path               file,
                                 ShaderCompileOptions                options,
                                 ShaderMaterial::TextureResolver     textures,
                                 std::vector<std::string>            requiredLightModes,
                                 ShaderAsset::SubshaderCompatibility compatible,
                                 const AssetSource*                  assetSource)
    {
        auto runtime = create(device,
                              std::move(file),
                              std::move(options),
                              legacyTextures(std::move(textures)),
                              std::move(requiredLightModes),
                              legacyCompatibility(std::move(compatible)),
                              assetSource);
        if (!runtime)
        {
            throw std::invalid_argument(runtime.error());
        }
        m_State = std::move((*runtime)->m_State);
    }

    ShaderRuntime::~ShaderRuntime() = default;

    std::expected<uint32_t, std::string> ShaderRuntime::addMaterialExpected(MaterialInstance instance)
    {
        if (m_State->publishing)
        {
            return std::unexpected("Cannot add a material during ShaderRuntime publication");
        }
        auto material      = std::make_unique<State::Material>();
        material->instance = std::move(instance);
        auto gpu           = m_State->createMaterialExpected(m_State->live->asset, material->instance);
        if (!gpu)
        {
            return std::unexpected(gpu.error());
        }
        material->gpu = std::move(*gpu);
        for (uint32_t index = 0; index < m_State->live->materials.size(); ++index)
        {
            if (!m_State->live->materials[index])
            {
                m_State->live->materials[index] = std::move(material);
                return index;
            }
        }
        if (m_State->live->materials.size() >= UINT32_MAX)
        {
            return std::unexpected("ShaderRuntime material indices exhausted");
        }
        m_State->live->materials.push_back(std::move(material));
        return uint32_t(m_State->live->materials.size() - 1);
    }

    std::expected<void, std::string> ShaderRuntime::removeMaterialExpected(uint32_t index)
    {
        if (m_State->publishing)
        {
            return std::unexpected("Cannot remove a material during ShaderRuntime publication");
        }
        auto material = materialExpected(index);
        if (!material)
        {
            return std::unexpected(material.error());
        }
        if (auto idle = m_State->device.waitIdleExpected(); !idle)
        {
            return idle;
        }
        m_State->live->materials[index].reset();
        return {};
    }

    std::expected<MaterialInstance*, std::string> ShaderRuntime::instanceExpected(uint32_t index)
    {
        if (m_State->publishing)
        {
            return std::unexpected("Cannot borrow a live instance during ShaderRuntime publication");
        }
        if (index >= m_State->live->materials.size() || !m_State->live->materials[index])
        {
            return std::unexpected("Shader material index is missing or removed: " + std::to_string(index));
        }
        return &m_State->live->materials[index]->instance;
    }

    std::expected<ShaderMaterial*, std::string> ShaderRuntime::materialExpected(uint32_t index)
    {
        if (m_State->publishing)
        {
            return std::unexpected("Cannot borrow a live material during ShaderRuntime publication");
        }
        if (index >= m_State->live->materials.size() || !m_State->live->materials[index])
        {
            return std::unexpected("Shader material index is missing or removed: " + std::to_string(index));
        }
        return m_State->live->materials[index]->gpu.get();
    }

    uint32_t ShaderRuntime::addMaterial(MaterialInstance instance)
    {
        auto index = addMaterialExpected(std::move(instance));
        if (!index)
        {
            throw std::invalid_argument(index.error());
        }
        return *index;
    }

    void ShaderRuntime::removeMaterial(uint32_t index)
    {
        if (auto removed = removeMaterialExpected(index); !removed)
        {
            throw std::invalid_argument(removed.error());
        }
    }

    const ShaderAsset& ShaderRuntime::asset() const
    {
        return m_State->live->asset;
    }

    MaterialInstance& ShaderRuntime::instance(uint32_t index)
    {
        auto instance = instanceExpected(index);
        if (!instance)
        {
            throw std::invalid_argument(instance.error());
        }
        return **instance;
    }

    ShaderMaterial& ShaderRuntime::material(uint32_t index)
    {
        auto material = materialExpected(index);
        if (!material)
        {
            throw std::invalid_argument(material.error());
        }
        return **material;
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

    std::expected<bool, std::string> ShaderRuntime::reloadExpected(const PrepareCandidateExpected& prepare)
    {
        auto& state = *m_State;
        if (state.publishing)
        {
            return state.reject("Cannot reload during ShaderRuntime publication");
        }
        const auto revision = state.revision.fetch_add(1, std::memory_order_relaxed) + 1;
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
        if (state.source)
        {
            state.source->started  = revision;
            state.source->observed = revision;
        }
#else
        static_cast<void>(revision);
#endif
        auto candidate = state.loadAssetExpected();
        if (!candidate)
        {
            return state.reject(candidate.error());
        }
        const auto compiledRevision = state.revision.load(std::memory_order_relaxed);
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
        if (state.source)
        {
            // Notifications for the triggering write may arrive during this synchronous compile.
            state.source->started  = compiledRevision;
            state.source->observed = compiledRevision;
        }
#endif
        return state.publishExpected(std::move(*candidate), compiledRevision, prepare);
    }

    bool ShaderRuntime::reload(const PrepareCandidate& prepare)
    {
        auto published = reloadExpected(legacyPreparation(prepare));
        return published && *published;
    }

#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
    std::expected<bool, std::string> ShaderRuntime::pollExpected(const PrepareCandidateExpected& prepare)
    {
        auto& state = *m_State;
        if (state.publishing)
        {
            return state.reject("Cannot poll during ShaderRuntime publication");
        }
        if (!state.source)
        {
            return false;
        }
        auto published = state.source->pollExpected(
            [&](ShaderAsset asset, uint64_t revision)
            {
                return state.publishExpected(std::move(asset), revision, prepare);
            },
            state.diagnostics);
        if (!published)
        {
            return state.reject(published.error());
        }
        return published;
    }

    bool ShaderRuntime::poll(const PrepareCandidate& prepare)
    {
        auto published = pollExpected(legacyPreparation(prepare));
        return published && *published;
    }
#endif
} // namespace vultra
