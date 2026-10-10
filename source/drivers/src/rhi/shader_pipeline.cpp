#include "shader_cache.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/shader_compiler.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4005 4068) // upstream FileWatch macros and #pragma mark
#endif
#include <FileWatch.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <xxhash.h>

#include <atomic>
#include <chrono>
#include <map>
#include <set>

namespace vultra
{
    struct ShaderPipeline::Watch
    {
        std::atomic<uint64_t>                 revision {0};
        uint64_t                              observed = 0;
        bool                                  pending  = false;
        std::chrono::steady_clock::time_point changed {};
        std::set<std::filesystem::path>       roots;
        std::set<std::filesystem::path>       dependencyDirectories;
        // Destroy first: FileWatch joins its threads before callback state is destroyed.
        std::map<std::filesystem::path, std::unique_ptr<filewatch::FileWatch<std::string>>> watchers;

        void refreshDirectories()
        {
            std::set<std::filesystem::path> directories = roots;
            for (const auto& directory : dependencyDirectories)
            {
                if (std::filesystem::is_directory(directory))
                {
                    directories.insert(directory);
                }
            }
#if defined(__linux__)
            // inotify watches one directory; FileWatch's Windows backend already watches subtrees.
            for (const auto& root : roots)
            {
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
                    directories.insert(entry->path());
                }
            }
#endif
            std::erase_if(watchers,
                          [&](const auto& entry)
                          {
                              return !directories.contains(entry.first);
                          });
            for (const auto& directory : directories)
            {
                if (!watchers.contains(directory))
                {
                    auto watcher = std::make_unique<filewatch::FileWatch<std::string>>(
                        directory.string(),
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
                            revision.fetch_add(1, std::memory_order_relaxed);
                        });
                    watchers.emplace(directory, std::move(watcher));
                }
            }
        }
    };

    ShaderPipeline::ShaderPipeline(Device&                            device,
                                   std::filesystem::path              file,
                                   std::vector<ShaderEntry>           entries,
                                   Builder                            builder,
                                   std::filesystem::path              watchDirectory,
                                   std::vector<std::filesystem::path> includeDirectories) :
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
            std::move(watchDirectory))
    {
    }

    ShaderPipeline::ShaderPipeline(Device&               device,
                                   std::filesystem::path file,
                                   ShaderCompileOptions  options,
                                   Builder               builder,
                                   std::filesystem::path watchDirectory,
                                   std::filesystem::path cacheDirectory) :
        m_Device(device),
        m_File(std::filesystem::absolute(file)),
        m_CompileOptions(std::move(options)),
        m_Builder(std::move(builder))
    {
        if (m_CompileOptions.entries.empty())
        {
            throw std::invalid_argument("Shader entry points cannot be empty");
        }
        if (m_File.extension() == ".slang" && !std::filesystem::exists(m_File))
        {
            m_File.replace_extension(".vshaderc");
        }
        if (m_File.extension() != ".slang" && m_File.extension() != ".vshaderc")
        {
            throw std::invalid_argument("ShaderPipeline requires a .slang source or .vshaderc program");
        }
        for (auto& directory : m_CompileOptions.includeDirectories)
        {
            directory = std::filesystem::absolute(directory).lexically_normal();
        }
        if (m_File.extension() == ".slang")
        {
            m_Compiler = std::make_unique<ShaderCompiler>(std::move(cacheDirectory));
            m_Watch    = std::make_unique<Watch>();
            m_Watch->roots.insert(watchDirectory.empty() ? m_File.parent_path() :
                                                           std::filesystem::absolute(watchDirectory));
        }
        if (!reload())
        {
            throw std::runtime_error(m_Diagnostics);
        }
    }

    ShaderPipeline::~ShaderPipeline()
    {
        m_Watch.reset();
        m_Device.waitIdle();
        if (m_Pipeline)
        {
            m_Device.core.DestroyPipeline(m_Pipeline);
        }
    }

    bool ShaderPipeline::poll()
    {
        if (!m_Watch)
        {
            return false;
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
        m_Watch->refreshDirectories();
        return reload();
    }

    bool ShaderPipeline::reload()
    {
        m_Diagnostics.clear();
        try
        {
            const auto& desc = *m_Device.core.GetDeviceDesc(m_Device.handle);
            if (desc.graphicsAPI != VriGraphicsAPI_Vulkan)
            {
                throw std::invalid_argument("SPIR-V shader programs require a Vulkan device");
            }
            const auto program = m_File.extension() == ".vshaderc" ? ShaderProgram::load(m_File) :
                                                                     m_Compiler->compile(m_File, m_CompileOptions);
            m_Diagnostics      = program.diagnostics;
            if (program.rayQuery && !desc.hasRayQuery)
            {
                throw std::invalid_argument("Shader program requires ray query: " + m_File.string());
            }
            if (m_Watch)
            {
                // Recurse the shader tree, not every third-party search root for every pipeline.
                m_Watch->dependencyDirectories =
                    detail::shaderWatchDirectories(program.dependencies, m_File, m_CompileOptions);
                m_Watch->refreshDirectories();
            }
            const auto             shaders = program.descriptors(m_CompileOptions.entries);
            ShaderPipelineIdentity identity {m_File, program.compileKey, 0, program.dependencies};
            for (const auto& shader : shaders)
            {
                const std::string_view entry = shader.entryPointName ? shader.entryPointName : "main";
                identity.spirvHash = XXH3_64bits_withSeed(&shader.stage, sizeof(shader.stage), identity.spirvHash);
                identity.spirvHash = XXH3_64bits_withSeed(entry.data(), entry.size(), identity.spirvHash);
                identity.spirvHash = XXH3_64bits_withSeed(shader.bytecode, shader.bytecodeSize, identity.spirvHash);
            }
            VriPipeline* replacement = m_Builder(shaders);
            if (!replacement)
            {
                throw std::runtime_error("Pipeline builder returned null");
            }
            m_Device.core.SetDebugName(replacement, m_File.filename().string().c_str());
            m_Device.waitIdle();
            if (m_Pipeline)
            {
                m_Device.core.DestroyPipeline(m_Pipeline);
            }
            m_Pipeline = replacement;
            m_Identity = std::move(identity);
            ++m_Generation;
            return true;
        }
        catch (const std::exception& error)
        {
            m_Diagnostics += error.what();
            Logger::core().error("[Shader] {}", m_Diagnostics);
            return false;
        }
    }
} // namespace vultra
