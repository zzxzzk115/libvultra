#include "rhi/shader_cache.hpp"
#include "shader_source_reload.hpp"

#include <vultra/core/base/logger.hpp>

#include <set>

namespace vultra::detail
{
    ShaderSourceReload::ShaderSourceReload(std::filesystem::path  file,
                                           ShaderCompileOptions   options,
                                           std::atomic<uint64_t>& revision) :
        file(std::move(file)),
        options(std::move(options)),
        revision(revision)
    {
    }

    ShaderSourceReload::~ShaderSourceReload()
    {
        // Join callbacks before the borrowed revision counter and job storage can expire.
        watchers.clear();
        scheduler.waitAll();
        job.reset();
    }

    void ShaderSourceReload::refreshWatch(const ShaderAsset& asset)
    {
        std::set<std::filesystem::path> roots {file.parent_path()};
        for (const auto& subshader : asset.subshaders)
        {
            for (const auto& pass : subshader.passes)
            {
                for (const auto& [name, program] : pass.programs)
                {
                    const auto directories = shaderWatchDirectories(program.dependencies, file, options);
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
                             std::make_unique<Watch>(root.string(),
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
                                                         if (extension.empty() || extension == ".vshader" ||
                                                             extension == ".slang" || extension == ".slangh" ||
                                                             extension == ".h")
                                                         {
                                                             revision.fetch_add(1, std::memory_order_relaxed);
                                                         }
                                                     }));
        }
    }

    bool ShaderSourceReload::dependenciesCurrent(const ShaderAsset& asset) const
    {
        for (const auto& subshader : asset.subshaders)
        {
            for (const auto& pass : subshader.passes)
            {
                for (const auto& [name, program] : pass.programs)
                {
                    if (!shaderDependenciesCurrent(program.dependencies, file, options))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool ShaderSourceReload::poll(const ShaderAsset& asset, const PublishCandidate& publish, std::string& diagnostics)
    {
        const auto sourceRevision = revision.load(std::memory_order_relaxed);
        if (sourceRevision != observed)
        {
            Logger::core().info("Shader source revision {} observed", sourceRevision);
            observed = sourceRevision;
            refreshWatch(asset);
            changed = std::chrono::steady_clock::now();
        }
        bool published = false;
        if (job && job->done.load(std::memory_order_acquire))
        {
            scheduler.wait(*job->task);
            if (job->revision == sourceRevision)
            {
                if (job->candidate)
                {
                    published = publish(std::move(*job->candidate), job->revision);
                }
                else
                {
                    diagnostics = job->diagnostics;
                    Logger::core().warn("Shader compilation failed; previous asset retained: {}", diagnostics);
                }
            }
            else
            {
                Logger::core().info("Discarded stale shader compile revision {}", job->revision);
            }
            job.reset();
        }
        if (!job && sourceRevision != started &&
            std::chrono::steady_clock::now() - changed >= std::chrono::milliseconds(150))
        {
            Logger::core().info("Compiling shader revision {}", sourceRevision);
            started                   = sourceRevision;
            job                       = std::make_unique<Job>();
            job->revision             = sourceRevision;
            auto*      currentJob     = job.get();
            const auto sourceFile     = file;
            const auto compileOptions = options;
            auto       compile        = [currentJob, sourceFile, compileOptions](vtask::Range)
            {
                try
                {
                    currentJob->candidate =
                        std::make_unique<ShaderAsset>(ShaderAsset::compile(sourceFile, compileOptions));
                }
                catch (const std::exception& error)
                {
                    currentJob->diagnostics = error.what();
                }
                currentJob->done.store(true, std::memory_order_release);
            };
            job->task = std::make_unique<vtask::TaskSet>(1, 1, std::move(compile));
            scheduler.run(*job->task);
        }
        return published;
    }
} // namespace vultra::detail
