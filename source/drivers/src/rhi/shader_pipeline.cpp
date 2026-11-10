#include "shader_archive.hpp"
#ifdef VULTRA_WITH_SHADER_COMPILER
#include "shader_cache.hpp"
#endif

#include <vultra/assets/asset_source.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

#ifdef VULTRA_WITH_SHADER_COMPILER
#include <vultra/drivers/rhi/shader_compiler.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4005 4068)
#endif
#include <FileWatch.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif

#include <xxhash.h>

#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        std::expected<std::filesystem::path, std::string> pipelinePath(const std::filesystem::path& path)
        {
            if (path.empty() ||
                path.native().find(std::filesystem::path::value_type(0)) != std::filesystem::path::string_type::npos)
            {
                return std::unexpected("Shader pipeline requires nonempty paths without NUL characters");
            }
            std::error_code error;
            auto            absolute = std::filesystem::absolute(path, error);
            if (error)
            {
                return std::unexpected("Resolve shader pipeline path: " + error.message());
            }
            return absolute.lexically_normal();
        }

        ShaderPipeline::ReflectedBuilder reflectedBuilder(ShaderPipeline::ExpectedBuilder builder)
        {
            if (!builder)
            {
                return {};
            }
            return [builder = std::move(builder)](const ShaderProgram&, std::span<const VriShaderDesc> shaders)
            {
                return builder(shaders);
            };
        }

        // Only existing throwing callback callers use this transitional adapter.
        ShaderPipeline::ExpectedBuilder legacyBuilder(ShaderPipeline::Builder builder)
        {
            if (!builder)
            {
                return {};
            }
            return [builder = std::move(builder)](
                       std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                try
                {
                    return builder(shaders);
                }
                catch (const std::exception& error)
                {
                    return std::unexpected(error.what());
                }
            };
        }
    } // namespace

#ifdef VULTRA_WITH_SHADER_COMPILER
    struct ShaderPipeline::Watch
    {
        using NativeWatch = filewatch::FileWatch<std::string>;

        struct PendingDirectories
        {
            std::set<std::filesystem::path>                               desired;
            std::map<std::filesystem::path, std::unique_ptr<NativeWatch>> additions;
        };

        std::atomic<uint64_t>                 revision {0};
        uint64_t                              observed = 0;
        bool                                  pending  = false;
        std::chrono::steady_clock::time_point changed {};
        std::set<std::filesystem::path>       roots;
        std::vector<ShaderDependency>         retryDependencies;
        // Destroy first: native watches join before their captured revision state dies.
        std::map<std::filesystem::path, std::unique_ptr<NativeWatch>> watchers;

        std::expected<PendingDirectories, std::string> prepareExpected(std::set<std::filesystem::path> dependencies)
        {
            PendingDirectories result;
            result.desired = roots;
            result.desired.insert(dependencies.begin(), dependencies.end());
            for (const auto& directory : result.desired)
            {
                std::error_code error;
                if (!std::filesystem::is_directory(directory, error) || error)
                {
                    return std::unexpected("Shader watch requires a directory: " + directory.string() +
                                           (error ? ": " + error.message() : ""));
                }
            }
#if defined(__linux__)
            // inotify watches one directory; Windows and FSEvents already observe subtrees.
            for (const auto& root : roots)
            {
                std::error_code                                     error;
                std::filesystem::recursive_directory_iterator       entry(root, error);
                const std::filesystem::recursive_directory_iterator end;
                while (!error && entry != end)
                {
                    if (entry->is_directory(error))
                    {
                        const auto name = entry->path().filename();
                        if (name == "build" || name == ".git" || name == ".vultra")
                        {
                            entry.disable_recursion_pending();
                        }
                        else
                        {
                            result.desired.insert(entry->path());
                        }
                    }
                    if (!error)
                    {
                        entry.increment(error);
                    }
                }
                if (error)
                {
                    return std::unexpected("Enumerate shader watch tree: " + error.message());
                }
            }
#endif
            for (const auto& directory : result.desired)
            {
                const auto existing = watchers.find(directory);
                if (existing != watchers.end() && existing->second->statusExpected())
                {
                    continue;
                }
                auto created = NativeWatch::createExpected(
                    directory.string(),
                    [this](const std::string& path, filewatch::Event) -> std::expected<void, std::string>
                    {
                        for (const auto& part : std::filesystem::path(path))
                        {
                            if (part == "build" || part == ".git" || part == ".vultra")
                            {
                                return {};
                            }
                        }
                        revision.fetch_add(1, std::memory_order_relaxed);
                        return {};
                    });
                if (!created)
                {
                    return std::unexpected("Start shader watch for " + directory.string() + ": " + created.error());
                }
                result.additions.emplace(directory, std::move(*created));
            }
            return result;
        }

        void commit(PendingDirectories candidate)
        {
            std::erase_if(watchers,
                          [&](const auto& entry)
                          {
                              return !candidate.desired.contains(entry.first) ||
                                     candidate.additions.contains(entry.first);
                          });
            watchers.merge(candidate.additions);
        }
    };
#endif

    void ShaderPipeline::DestroyPipeline::operator()(VriPipeline* pipeline) const noexcept
    {
        device->core.DestroyPipeline(pipeline);
    }

    ShaderPipeline::ShaderPipeline(Device& device, Uninitialized) :
        m_Device(device),
        m_Pipeline(nullptr, DestroyPipeline {&device})
    {
    }

    std::expected<std::unique_ptr<ShaderPipeline>, std::string> ShaderPipeline::create(Device&                  device,
                                                                                       const ShaderProgram&     program,
                                                                                       std::vector<ShaderEntry> entries,
                                                                                       ExpectedBuilder          builder)
    {
        auto pipeline       = std::unique_ptr<ShaderPipeline>(new ShaderPipeline(device, Uninitialized {}));
        pipeline->m_Builder                = reflectedBuilder(std::move(builder));
        pipeline->m_CompileOptions.entries = std::move(entries);
        if (auto replaced = pipeline->replaceExpected(program); !replaced)
        {
            return std::unexpected(replaced.error());
        }
        pipeline->m_Builder = {}; // Immutable programs never invoke the builder again.
        return pipeline;
    }

    std::expected<std::unique_ptr<ShaderPipeline>, std::string>
    ShaderPipeline::create(Device&                            device,
                           std::filesystem::path              file,
                           std::vector<ShaderEntry>           entries,
                           ExpectedBuilder                    builder,
                           std::filesystem::path              watchDirectory,
                           std::vector<std::filesystem::path> includeDirectories,
                           const AssetSource*                 source)
    {
        const auto* description = device.core.GetDeviceDesc(device.handle);
        if (!description)
        {
            return std::unexpected("Device description is unavailable");
        }
        ShaderCompileOptions options;
        options.entries            = std::move(entries);
        options.includeDirectories = std::move(includeDirectories);
        options.rayQuery           = description->hasRayQuery;
        return create(device,
                      std::move(file),
                      std::move(options),
                      std::move(builder),
                      std::move(watchDirectory),
                      ".vultra/shaders",
                      source);
    }

    std::expected<std::unique_ptr<ShaderPipeline>, std::string>
    ShaderPipeline::create(Device&               device,
                           std::filesystem::path file,
                           ShaderCompileOptions  options,
                           ExpectedBuilder       builder,
                           std::filesystem::path watchDirectory,
                           std::filesystem::path cacheDirectory,
                           const AssetSource*    source)
    {
        return create(device,
                      std::move(file),
                      std::move(options),
                      reflectedBuilder(std::move(builder)),
                      std::move(watchDirectory),
                      std::move(cacheDirectory),
                      source);
    }

    std::expected<std::unique_ptr<ShaderPipeline>, std::string>
    ShaderPipeline::create(Device&               device,
                           std::filesystem::path file,
                           ShaderCompileOptions  options,
                           ReflectedBuilder      builder,
                           std::filesystem::path watchDirectory,
                           std::filesystem::path cacheDirectory,
                           const AssetSource*    source)
    {
        auto pipeline       = std::unique_ptr<ShaderPipeline>(new ShaderPipeline(device, Uninitialized {}));
        pipeline->m_Builder = std::move(builder);
        if (auto ready = pipeline->initializeExpected(std::move(file),
                                                      std::move(options),
                                                      std::move(watchDirectory),
                                                      std::move(cacheDirectory),
                                                      source);
            !ready)
        {
            return std::unexpected(ready.error());
        }
        return pipeline;
    }

    std::expected<void, std::string> ShaderPipeline::initializeExpected(std::filesystem::path file,
                                                                        ShaderCompileOptions  options,
                                                                        std::filesystem::path watchDirectory,
                                                                        std::filesystem::path cacheDirectory,
                                                                        const AssetSource*    source)
    {
        if (!m_Builder || options.entries.empty())
        {
            return std::unexpected("Shader pipeline requires a builder and entry points");
        }
        auto identity = source ? source->resolveExpected(file) : pipelinePath(file);
        if (!identity)
        {
            return std::unexpected(identity.error());
        }
        m_File = std::move(*identity);
        if (m_File.extension() == ".slang")
        {
            std::error_code error;
            const bool      exists = source ? false : std::filesystem::exists(m_File, error);
            if (error)
            {
                return std::unexpected("Inspect shader source: " + error.message());
            }
            if (!exists)
            {
                m_File.replace_extension(".vshaderc");
            }
        }
        if (m_File.extension() != ".slang" && m_File.extension() != ".vshaderc")
        {
            return std::unexpected("ShaderPipeline requires a .slang source or .vshaderc program");
        }
        for (auto& directory : options.includeDirectories)
        {
            auto absolute = pipelinePath(directory);
            if (!absolute)
            {
                return std::unexpected(absolute.error());
            }
            directory = std::move(*absolute);
        }
        m_AssetSource    = source;
        m_CompileOptions = std::move(options);
        if (m_File.extension() == ".slang")
        {
#ifdef VULTRA_WITH_SHADER_COMPILER
            auto compiler = ShaderCompiler::create(std::move(cacheDirectory));
            if (!compiler)
            {
                return std::unexpected(compiler.error());
            }
            m_Compiler = std::move(*compiler);
            auto root  = pipelinePath(watchDirectory.empty() ? m_File.parent_path() : watchDirectory);
            if (!root)
            {
                return std::unexpected(root.error());
            }
            m_Watch = std::make_unique<Watch>();
            m_Watch->roots.insert(std::move(*root));
#else
            return std::unexpected("This build loads cooked .vshaderc programs; compile Slang on the host");
#endif
        }
        return reloadExpected();
    }

    std::expected<void, std::string> ShaderPipeline::rejectReload(std::string diagnostic)
    {
        m_Diagnostics = std::move(diagnostic);
        return std::unexpected(m_Diagnostics);
    }

    std::expected<void, std::string> ShaderPipeline::reloadExpected()
    {
        return reloadWithCompletionsExpected(nullptr);
    }

    std::expected<void, std::string> ShaderPipeline::reloadExpected(std::vector<SubmissionCompletion>& lastUses)
    {
        return reloadWithCompletionsExpected(&lastUses);
    }

    std::expected<void, std::string>
    ShaderPipeline::reloadWithCompletionsExpected(std::vector<SubmissionCompletion>* lastUses)
    {
        if (m_File.empty())
        {
            return rejectReload("An in-memory shader pipeline has no file to reload");
        }
#ifdef VULTRA_WITH_SHADER_COMPILER
        if (m_Watch)
        {
            // Recompute existing parents so removed former includes cannot block a valid source edit.
            auto dependencies = m_Identity.dependencies;
            dependencies.insert(dependencies.end(),
                                m_Watch->retryDependencies.begin(),
                                m_Watch->retryDependencies.end());
            auto directories = detail::shaderWatchDirectoriesExpected(dependencies, m_File, m_CompileOptions);
            if (!directories)
            {
                return rejectReload(directories.error());
            }
            auto current = m_Watch->prepareExpected(std::move(*directories));
            if (!current)
            {
                return rejectReload(current.error());
            }
            m_Watch->commit(std::move(*current));
        }
#endif
        auto program = [&]() -> std::expected<ShaderProgram, std::string>
        {
            if (m_AssetSource)
            {
                auto bytes = m_AssetSource->readExpected(m_File);
                if (!bytes)
                {
                    return std::unexpected(bytes.error());
                }
                return ShaderProgram::loadExpected(*bytes, m_File);
            }
#ifdef VULTRA_WITH_SHADER_COMPILER
            if (m_Compiler)
            {
                return m_Compiler->compileExpected(m_File, m_CompileOptions);
            }
#endif
            return ShaderProgram::loadExpected(m_File);
        }();
        if (!program)
        {
            return rejectReload(program.error());
        }
#ifdef VULTRA_WITH_SHADER_COMPILER
        std::optional<Watch::PendingDirectories> pending;
        if (m_Watch)
        {
            auto directories = detail::shaderWatchDirectoriesExpected(program->dependencies, m_File, m_CompileOptions);
            if (!directories)
            {
                return rejectReload(directories.error());
            }
            auto prepared = m_Watch->prepareExpected(std::move(*directories));
            if (!prepared)
            {
                return rejectReload(prepared.error());
            }
            pending.emplace(std::move(*prepared));
        }
#endif
        if (auto replaced = replaceExpected(*program, lastUses); !replaced)
        {
#ifdef VULTRA_WITH_SHADER_COMPILER
            if (pending)
            {
                // Retain accepted and rejected-candidate inputs until a replacement succeeds.
                for (const auto& entry : m_Watch->watchers)
                {
                    pending->desired.insert(entry.first);
                }
                m_Watch->retryDependencies = program->dependencies;
                m_Watch->commit(std::move(*pending));
            }
#endif
            return rejectReload(program->diagnostics + replaced.error());
        }
#ifdef VULTRA_WITH_SHADER_COMPILER
        if (pending)
        {
            m_Watch->commit(std::move(*pending));
            m_Watch->retryDependencies.clear();
        }
#endif
        m_Diagnostics = std::move(program->diagnostics);
        return {};
    }

    std::expected<bool, std::string> ShaderPipeline::pollExpected()
    {
#ifdef VULTRA_WITH_SHADER_COMPILER
        if (!m_Watch)
        {
            return false;
        }
        for (const auto& [directory, watcher] : m_Watch->watchers)
        {
            if (auto status = watcher->statusExpected(); !status)
            {
                m_Diagnostics = "Shader watch for " + directory.string() + ": " + status.error();
                return std::unexpected(m_Diagnostics);
            }
        }
        const auto revision = m_Watch->revision.load(std::memory_order_relaxed);
        const auto now      = std::chrono::steady_clock::now();
        if (revision != m_Watch->observed)
        {
            m_Watch->observed = revision;
            m_Watch->changed  = now;
            m_Watch->pending  = true;
        }
        if (!m_Watch->pending || now - m_Watch->changed < std::chrono::milliseconds(150))
        {
            return false;
        }
        m_Watch->pending = false;
        if (auto reloaded = reloadExpected(); !reloaded)
        {
            return std::unexpected(reloaded.error());
        }
        return true;
#else
        return false;
#endif
    }

    std::expected<void, std::string> ShaderPipeline::replaceExpected(const ShaderProgram&               program,
                                                                     std::vector<SubmissionCompletion>* lastUses)
    {
        if (!m_Builder)
        {
            return std::unexpected("Shader pipeline builder is empty");
        }
        const auto* description = m_Device.core.GetDeviceDesc(m_Device.handle);
        if (!description)
        {
            return std::unexpected("Device description is unavailable");
        }
        if (auto valid = detail::validateShaderProgramExpected(program, *description); !valid)
        {
            return valid;
        }
        auto shaders = program.descriptorsExpected(m_CompileOptions.entries);
        if (!shaders)
        {
            return std::unexpected(shaders.error());
        }
        ShaderPipelineIdentity identity {m_File, program.compileKey, 0, program.dependencies};
        for (const auto& shader : *shaders)
        {
            const std::string_view entry = shader.entryPointName ? shader.entryPointName : "main";
            identity.bytecodeHash = XXH3_64bits_withSeed(&shader.stage, sizeof(shader.stage), identity.bytecodeHash);
            identity.bytecodeHash = XXH3_64bits_withSeed(entry.data(), entry.size(), identity.bytecodeHash);
            identity.bytecodeHash = XXH3_64bits_withSeed(shader.bytecode, shader.bytecodeSize, identity.bytecodeHash);
        }
        auto built = m_Builder(program, *shaders);
        if (!built)
        {
            return std::unexpected(built.error());
        }
        if (!*built || *built == m_Pipeline.get())
        {
            return std::unexpected("Pipeline builder must return a new nonnull pipeline");
        }
        std::unique_ptr<VriPipeline, DestroyPipeline> candidate(*built, DestroyPipeline {&m_Device});
        m_Device.core.SetDebugName(candidate.get(),
                                   m_File.empty() ? "in-memory shader pipeline" : m_File.filename().string().c_str());
        if (m_Pipeline)
        {
            if (lastUses)
            {
                if (auto retired = m_Device.deferReleaseExpected(m_Pipeline, *lastUses); !retired)
                {
                    return std::unexpected("Retire previous shader pipeline: " + retired.error());
                }
            }
            else
            {
                if (auto idle = m_Device.waitIdleExpected(); !idle)
                {
                    return std::unexpected("Retire previous shader pipeline: " + idle.error());
                }
                m_Pipeline.reset();
            }
        }
        m_Pipeline = std::move(candidate);
        m_Identity = std::move(identity);
        ++m_Generation;
        return {};
    }

    ShaderPipeline::ShaderPipeline(Device&                            device,
                                   std::filesystem::path              file,
                                   std::vector<ShaderEntry>           entries,
                                   Builder                            builder,
                                   std::filesystem::path              watchDirectory,
                                   std::vector<std::filesystem::path> includeDirectories,
                                   const AssetSource*                 source) :
        ShaderPipeline(
            device,
            std::move(file),
            [&]
            {
                ShaderCompileOptions options;
                options.entries            = std::move(entries);
                options.includeDirectories = std::move(includeDirectories);
                options.rayQuery           = device.core.GetDeviceDesc(device.handle)->hasRayQuery;
                return options;
            }(),
            std::move(builder),
            std::move(watchDirectory),
            ".vultra/shaders",
            source)
    {
    }

    ShaderPipeline::ShaderPipeline(Device&               device,
                                   std::filesystem::path file,
                                   ShaderCompileOptions  options,
                                   Builder               builder,
                                   std::filesystem::path watchDirectory,
                                   std::filesystem::path cacheDirectory,
                                   const AssetSource*    source) :
        ShaderPipeline(device, Uninitialized {})
    {
        m_Builder = reflectedBuilder(legacyBuilder(std::move(builder)));
        if (auto ready = initializeExpected(std::move(file),
                                            std::move(options),
                                            std::move(watchDirectory),
                                            std::move(cacheDirectory),
                                            source);
            !ready)
        {
            throw std::runtime_error(ready.error());
        }
    }

    ShaderPipeline::ShaderPipeline(Device&                  device,
                                   const ShaderProgram&     program,
                                   std::vector<ShaderEntry> entries,
                                   Builder                  builder) :
        ShaderPipeline(device, Uninitialized {})
    {
        m_Builder                = reflectedBuilder(legacyBuilder(std::move(builder)));
        m_CompileOptions.entries = std::move(entries);
        if (auto ready = replaceExpected(program); !ready)
        {
            throw std::runtime_error(ready.error());
        }
        m_Builder = {};
    }

    bool ShaderPipeline::reload()
    {
        auto reloaded = reloadExpected();
        if (!reloaded)
        {
            Logger::core().error("[Shader] {}", reloaded.error());
        }
        return bool(reloaded);
    }

    bool ShaderPipeline::poll()
    {
        auto changed = pollExpected();
        if (!changed)
        {
            Logger::core().error("[Shader] {}", changed.error());
            return false;
        }
        return *changed;
    }

    ShaderPipeline::~ShaderPipeline()
    {
#ifdef VULTRA_WITH_SHADER_COMPILER
        m_Watch.reset();
#endif
        if (m_Pipeline)
        {
            m_Device.waitIdleForDestruction();
            m_Pipeline.reset();
        }
    }
} // namespace vultra
