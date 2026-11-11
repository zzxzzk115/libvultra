#include "shader_archive.hpp"
#ifdef VULTRA_WITH_SHADER_COMPILER
#include "shader_cache.hpp"
#endif

#include <vultra/assets/asset_source.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#ifdef VULTRA_WITH_SHADER_COMPILER
#include <vultra/platform/os/file_watcher.hpp>
#include <vultra/shader/shader_compiler.hpp>
#endif

#include <xxhash.h>

#include <set>

namespace vultra
{
#ifdef VULTRA_WITH_SHADER_COMPILER
    struct ShaderPipeline::Watch
    {
        std::set<std::filesystem::path> roots;
        std::set<std::filesystem::path> dependencyDirectories;
        FileWatcher                     watcher;

        void refreshDirectories()
        {
            std::vector<std::filesystem::path> directories(roots.begin(), roots.end());
            directories.insert(directories.end(), dependencyDirectories.begin(), dependencyDirectories.end());
            watcher.watch(directories);
        }
    };

#endif

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
        m_Device(device),
        m_AssetSource(source),
        m_File(source ? source->resolve(file) : std::filesystem::absolute(file)),
        m_CompileOptions(std::move(options)),
        m_Builder(std::move(builder))
    {
        if (m_CompileOptions.entries.empty())
        {
            throw std::invalid_argument("Shader entry points cannot be empty");
        }
        if (m_File.extension() == ".slang" && (source || !std::filesystem::exists(m_File)))
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
#ifdef VULTRA_WITH_SHADER_COMPILER
            m_Compiler = std::make_unique<ShaderCompiler>(std::move(cacheDirectory));
            m_Watch    = std::make_unique<Watch>();
            m_Watch->roots.insert(watchDirectory.empty() ? m_File.parent_path() :
                                                           std::filesystem::absolute(watchDirectory));
#else
            throw std::invalid_argument("This build loads cooked .vshaderc programs; compile Slang on the host");
#endif
        }
        if (!reload())
        {
            throw std::runtime_error(m_Diagnostics);
        }
    }

    ShaderPipeline::~ShaderPipeline()
    {
#ifdef VULTRA_WITH_SHADER_COMPILER
        m_Watch.reset();
#endif
        m_Device.waitIdleForDestruction();
        if (m_Pipeline)
        {
            m_Device.core.DestroyPipeline(m_Pipeline);
        }
    }

    bool ShaderPipeline::poll()
    {
#ifdef VULTRA_WITH_SHADER_COMPILER
        if (!m_Watch)
        {
            return false;
        }
        // Any change under the watched shader directories may be an include of this program.
        if (m_Watch->watcher.poll().empty())
        {
            return false;
        }
        m_Watch->refreshDirectories();
        return reload();
#else
        return false;
#endif
    }

    bool ShaderPipeline::reload()
    {
        m_Diagnostics.clear();
        try
        {
            if (m_File.empty())
            {
                throw std::logic_error("An in-memory shader pipeline has no file to reload");
            }
            ShaderProgram program;
            if (m_AssetSource)
            {
                program = ShaderProgram::load(m_AssetSource->read(m_File), m_File);
            }
            else
            {
#ifdef VULTRA_WITH_SHADER_COMPILER
                program = m_Compiler ? m_Compiler->compile(m_File, m_CompileOptions) : ShaderProgram::load(m_File);
#else
                program = ShaderProgram::load(m_File);
#endif
            }
            m_Diagnostics = program.diagnostics;
#ifdef VULTRA_WITH_SHADER_COMPILER
            if (m_Watch)
            {
                // Recurse the shader tree, not every third-party search root for every pipeline.
                m_Watch->dependencyDirectories =
                    detail::shaderWatchDirectories(program.dependencies, m_File, m_CompileOptions);
                m_Watch->refreshDirectories();
            }
#endif
            replace(program);
            return true;
        }
        catch (const std::exception& error)
        {
            m_Diagnostics += error.what();
            Logger::core().error("[Shader] {}", m_Diagnostics);
            return false;
        }
    }

    ShaderPipeline::ShaderPipeline(Device&                  device,
                                   const ShaderProgram&     program,
                                   std::vector<ShaderEntry> entries,
                                   Builder                  builder) :
        m_Device(device),
        m_Builder(std::move(builder))
    {
        m_CompileOptions.entries = std::move(entries);
        replace(program);
    }

    void ShaderPipeline::replace(const ShaderProgram& program)
    {
        const auto& desc = *m_Device.core.GetDeviceDesc(m_Device.handle);
        detail::validateShaderProgram(program, desc);
        const auto             shaders = program.descriptors(m_CompileOptions.entries);
        ShaderPipelineIdentity identity {m_File, program.compileKey, 0, program.dependencies};
        for (const auto& shader : shaders)
        {
            const std::string_view entry = shader.entryPointName ? shader.entryPointName : "main";
            identity.bytecodeHash = XXH3_64bits_withSeed(&shader.stage, sizeof(shader.stage), identity.bytecodeHash);
            identity.bytecodeHash = XXH3_64bits_withSeed(entry.data(), entry.size(), identity.bytecodeHash);
            identity.bytecodeHash = XXH3_64bits_withSeed(shader.bytecode, shader.bytecodeSize, identity.bytecodeHash);
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
    }
} // namespace vultra
