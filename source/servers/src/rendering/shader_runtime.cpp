#include "rhi/shader_cache.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/servers/rendering/shader_runtime.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4005 4068)
#endif
#include <FileWatch.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <set>

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

        struct Job
        {
            uint64_t                        revision = 0;
            std::atomic_bool                done {false};
            std::unique_ptr<ShaderAsset>    candidate;
            std::string                     diagnostics;
            std::unique_ptr<vtask::TaskSet> task;
        };

        Device&                               device;
        std::filesystem::path                 file;
        const AssetSource*                    assetSource;
        ShaderCompileOptions                  options;
        ShaderMaterial::TextureResolver       textures;
        std::vector<std::string>              requiredLightModes;
        ShaderAsset::SubshaderCompatibility   compatible;
        std::unique_ptr<Snapshot>             live;
        std::string                           diagnostics;
        uint64_t                              generation = 1;
        std::atomic<uint64_t>                 revision {0};
        uint64_t                              observed = 0;
        uint64_t                              started  = 0;
        std::chrono::steady_clock::time_point changed {};
        // vtask counts the calling thread; two threads provide one background compiler worker.
        vtask::Scheduler     scheduler {2};
        std::unique_ptr<Job> job;
        // Watch callbacks are joined before their revision storage or asynchronous job is destroyed.
        std::map<std::filesystem::path, std::unique_ptr<filewatch::FileWatch<std::string>>> watchers;

        State(Device&                             device,
              std::filesystem::path               file,
              ShaderCompileOptions                options,
              ShaderMaterial::TextureResolver     textures,
              std::vector<std::string>            requiredLightModes,
              ShaderAsset::SubshaderCompatibility compatible,
              const AssetSource*                  assetSource) :
            device(device),
            file(std::filesystem::absolute(file)),
            assetSource(assetSource),
            options(std::move(options)),
            textures(std::move(textures)),
            requiredLightModes(std::move(requiredLightModes)),
            compatible(std::move(compatible))
        {
            live        = std::make_unique<Snapshot>();
            live->asset = this->file.extension() == ".vshaderc" ? ShaderAsset::load(this->file, assetSource) :
                                                                  ShaderAsset::compile(this->file, this->options);
            diagnostics = live->asset.diagnostics;
            refreshWatch(live->asset);
        }

        ~State()
        {
            watchers.clear();
            scheduler.waitAll();
            job.reset();
            device.waitIdle();
            live.reset();
        }

        void refreshWatch(const ShaderAsset& asset)
        {
            if (file.extension() == ".vshaderc")
            {
                return;
            }
            std::set<std::filesystem::path> roots {file.parent_path()};
            for (const auto& subshader : asset.subshaders)
            {
                for (const auto& pass : subshader.passes)
                {
                    for (const auto& [name, program] : pass.programs)
                    {
                        const auto directories = detail::shaderWatchDirectories(program.dependencies, file, options);
                        roots.insert(directories.begin(), directories.end());
                    }
                }
            }
#if defined(__linux__)
            // Only the authored shader tree needs recursive discovery; dependency/search parents suffice elsewhere.
            const auto root = file.parent_path();
            for (auto entry = std::filesystem::recursive_directory_iterator(root);
                 entry != std::filesystem::recursive_directory_iterator();
                 ++entry)
            {
                if (!entry->is_directory())
                {
                    continue;
                }
                const auto name = entry->path().filename();
                if (name == "build" || name == ".git" || name == ".vultra")
                {
                    entry.disable_recursion_pending();
                    continue;
                }
                roots.insert(entry->path());
            }
#endif
            for (const auto& root : roots)
            {
                if (watchers.contains(root) || !std::filesystem::is_directory(root))
                {
                    continue;
                }
                watchers.emplace(root,
                                 std::make_unique<filewatch::FileWatch<std::string>>(
                                     root.string(),
                                     [this](const std::string& path, filewatch::Event)
                                     {
                                         const std::filesystem::path changed(path);
                                         for (const auto& part : changed)
                                         {
                                             if (part == "build" || part == ".git" || part == ".vultra")
                                             {
                                                 return;
                                             }
                                         }
                                         const auto extension = changed.extension();
                                         if (extension.empty() || extension == ".vshader" || extension == ".slang" ||
                                             extension == ".slangh" || extension == ".h")
                                         {
                                             revision.fetch_add(1, std::memory_order_relaxed);
                                         }
                                     }));
            }
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
                if (file.extension() == ".vshader")
                {
                    for (const auto& subshader : candidate->asset.subshaders)
                    {
                        for (const auto& pass : subshader.passes)
                        {
                            for (const auto& [name, program] : pass.programs)
                            {
                                if (!detail::shaderDependenciesCurrent(program.dependencies, file, options))
                                {
                                    // FileWatch delivery can lag a write performed during GPU preparation.
                                    revision.fetch_add(1, std::memory_order_relaxed);
                                    diagnostics =
                                        "Discarded shader candidate: dependency changed during GPU preparation";
                                    Logger::core().info("{}", diagnostics);
                                    return false;
                                }
                            }
                        }
                    }
                }
                refreshWatch(candidate->asset);
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

    ShaderRuntime::ShaderRuntime(Device&                             device,
                                 std::filesystem::path               source,
                                 ShaderCompileOptions                options,
                                 ShaderMaterial::TextureResolver     textures,
                                 std::vector<std::string>            requiredLightModes,
                                 ShaderAsset::SubshaderCompatibility compatible,
                                 const AssetSource*                  assetSource) :
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

    bool ShaderRuntime::compiling() const
    {
        return m_State->job != nullptr;
    }

    bool ShaderRuntime::reload(const PrepareCandidate& prepare)
    {
        auto&      state    = *m_State;
        const auto revision = state.revision.fetch_add(1, std::memory_order_relaxed) + 1;
        state.started       = revision;
        state.observed      = revision;
        try
        {
            auto candidate = state.file.extension() == ".vshaderc" ? ShaderAsset::load(state.file, state.assetSource) :
                                                                     ShaderAsset::compile(state.file, state.options);
            // Notifications for the write that triggered this synchronous compile may arrive during it.
            // Publication still checks dependency bytes and rejects edits made during GPU preparation.
            const auto compiledRevision = state.revision.load(std::memory_order_relaxed);
            state.started               = compiledRevision;
            state.observed              = compiledRevision;
            return state.publish(std::move(candidate), compiledRevision, prepare);
        }
        catch (const std::exception& error)
        {
            state.diagnostics = error.what();
            Logger::core().warn("Shader compilation failed; previous asset retained: {}", state.diagnostics);
            return false;
        }
    }

    bool ShaderRuntime::poll(const PrepareCandidate& prepare)
    {
        auto& state = *m_State;
        if (state.file.extension() == ".vshaderc")
        {
            return false;
        }
        const auto revision = state.revision.load(std::memory_order_relaxed);
        if (revision != state.observed)
        {
            Logger::core().info("Shader source revision {} observed", revision);
            state.observed = revision;
            state.refreshWatch(state.live->asset);
            state.changed = std::chrono::steady_clock::now();
        }
        bool published = false;
        if (state.job && state.job->done.load(std::memory_order_acquire))
        {
            state.scheduler.wait(*state.job->task);
            if (state.job->revision == revision)
            {
                if (state.job->candidate)
                {
                    published = state.publish(std::move(*state.job->candidate), state.job->revision, prepare);
                }
                else
                {
                    state.diagnostics = state.job->diagnostics;
                    Logger::core().warn("Shader compilation failed; previous asset retained: {}", state.diagnostics);
                }
            }
            else
            {
                Logger::core().info("Discarded stale shader compile revision {}", state.job->revision);
            }
            state.job.reset();
        }
        if (!state.job && revision != state.started &&
            std::chrono::steady_clock::now() - state.changed >= std::chrono::milliseconds(150))
        {
            Logger::core().info("Compiling shader revision {}", revision);
            state.started       = revision;
            state.job           = std::make_unique<State::Job>();
            state.job->revision = revision;
            auto*      job      = state.job.get();
            const auto file     = state.file;
            const auto options  = state.options;
            job->task           = std::make_unique<vtask::TaskSet>(1,
                                                                   1,
                                                                   [job, file, options](vtask::Range)
                                                                   {
                                                             try
                                                             {
                                                                 job->candidate = std::make_unique<ShaderAsset>(
                                                                     ShaderAsset::compile(file, options));
                                                             }
                                                             catch (const std::exception& error)
                                                             {
                                                                 job->diagnostics = error.what();
                                                             }
                                                             job->done.store(true, std::memory_order_release);
                                                                   });
            state.scheduler.run(*job->task);
        }
        return published;
    }
} // namespace vultra
