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

#include <expected>
#include <set>

namespace vultra
{
#ifdef VULTRA_WITH_SHADER_COMPILER
    struct ShaderPipeline::Watch
    {
        std::set<std::filesystem::path> roots;
        std::set<std::filesystem::path> dependencyDirectories;
        FileWatcher                     watcher;

        [[nodiscard]] std::expected<void, Error> refreshDirectories()
        {
            std::vector<std::filesystem::path> directories(roots.begin(), roots.end());
            directories.insert(directories.end(), dependencyDirectories.begin(), dependencyDirectories.end());
            return watcher.watch(directories);
        }
    };

#endif

    ShaderPipeline::ShaderPipeline(Device&               device,
                                   std::filesystem::path file,
                                   ShaderCompileOptions  options,
                                   Builder               builder,
                                   const AssetSource*    source) :
        m_Device(device),
        m_AssetSource(source),
        m_File(std::move(file)),
        m_CompileOptions(std::move(options)),
        m_Builder(std::move(builder))
    {
    }

    std::expected<std::unique_ptr<ShaderPipeline>, Error>
    ShaderPipeline::create(Device&                            device,
                           std::filesystem::path              file,
                           std::vector<ShaderEntry>           entries,
                           Builder                            builder,
                           std::filesystem::path              watchDirectory,
                           std::vector<std::filesystem::path> includeDirectories,
                           const AssetSource*                 source)
    {
        ShaderCompileOptions options;
        options.entries            = std::move(entries);
        options.includeDirectories = std::move(includeDirectories);
        options.rayQuery           = device.core.GetDeviceDesc(device.handle)->hasRayQuery;
        return create(device,
                      std::move(file),
                      std::move(options),
                      std::move(builder),
                      std::move(watchDirectory),
                      ".vultra/shaders",
                      source);
    }

    std::expected<std::unique_ptr<ShaderPipeline>, Error> ShaderPipeline::create(Device&               device,
                                                                                 std::filesystem::path file,
                                                                                 ShaderCompileOptions  options,
                                                                                 Builder               builder,
                                                                                 std::filesystem::path watchDirectory,
                                                                                 std::filesystem::path cacheDirectory,
                                                                                 const AssetSource*    source)
    {
        if (options.entries.empty())
        {
            return makeError("Shader entry points cannot be empty");
        }
        auto resolved = source ? source->resolve(file) : detail::absolutePath(file);
        if (!resolved)
        {
            return std::unexpected(resolved.error());
        }
        // A status that cannot be read counts as a missing source; the cooked sibling then reports the failure.
        std::error_code status;
        if (resolved->extension() == ".slang" && (source || !std::filesystem::exists(*resolved, status)))
        {
            resolved->replace_extension(".vshaderc");
        }
        if (resolved->extension() != ".slang" && resolved->extension() != ".vshaderc")
        {
            return makeError("ShaderPipeline requires a .slang source or .vshaderc program");
        }
        for (auto& directory : options.includeDirectories)
        {
            const auto absolute = detail::absolutePath(directory);
            if (!absolute)
            {
                return std::unexpected(absolute.error());
            }
            directory = absolute->lexically_normal();
        }
        std::unique_ptr<ShaderPipeline> pipeline(
            new ShaderPipeline(device, std::move(*resolved), std::move(options), std::move(builder), source));
        if (pipeline->m_File.extension() == ".slang")
        {
#ifdef VULTRA_WITH_SHADER_COMPILER
            auto compiler = ShaderCompiler::create(cacheDirectory);
            if (!compiler)
            {
                return std::unexpected(compiler.error());
            }
            auto root = watchDirectory.empty() ? pipeline->m_File.parent_path() : detail::absolutePath(watchDirectory);
            if (!root)
            {
                return std::unexpected(root.error());
            }
            pipeline->m_Compiler = std::make_unique<ShaderCompiler>(std::move(*compiler));
            pipeline->m_Watch    = std::make_unique<Watch>();
            pipeline->m_Watch->roots.insert(std::move(*root));
#else
            return makeError("This build loads cooked .vshaderc programs; compile Slang on the host");
#endif
        }
        if (!pipeline->reload())
        {
            return std::unexpected(Error {pipeline->m_Diagnostics});
        }
        return pipeline;
    }

    std::expected<std::unique_ptr<ShaderPipeline>, Error> ShaderPipeline::create(Device&                  device,
                                                                                 const ShaderProgram&     program,
                                                                                 std::vector<ShaderEntry> entries,
                                                                                 Builder                  builder)
    {
        ShaderCompileOptions options;
        options.entries = std::move(entries);
        std::unique_ptr<ShaderPipeline> pipeline(
            new ShaderPipeline(device, {}, std::move(options), std::move(builder), nullptr));
        if (auto replaced = pipeline->replace(program); !replaced)
        {
            return std::unexpected(replaced.error());
        }
        return pipeline;
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
        if (auto watched = m_Watch->refreshDirectories(); !watched)
        {
            m_Diagnostics = watched.error().message;
            Logger::core().error("[Shader] {}", m_Diagnostics);
            return false;
        }
        return reload();
#else
        return false;
#endif
    }

    bool ShaderPipeline::reload()
    {
        m_Diagnostics.clear();
        if (auto loaded = load(); !loaded)
        {
            m_Diagnostics += loaded.error().message;
            Logger::core().error("[Shader] {}", m_Diagnostics);
            return false;
        }
        return true;
    }

    std::expected<void, Error> ShaderPipeline::load()
    {
        if (m_File.empty())
        {
            return makeError("An in-memory shader pipeline has no file to reload");
        }
        std::expected<ShaderProgram, Error> program;
        if (m_AssetSource)
        {
            const auto bytes = m_AssetSource->read(m_File);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            program = ShaderProgram::load(*bytes, m_File);
        }
        else
        {
#ifdef VULTRA_WITH_SHADER_COMPILER
            program = m_Compiler ? m_Compiler->compile(m_File, m_CompileOptions) : ShaderProgram::load(m_File);
#else
            program = ShaderProgram::load(m_File);
#endif
        }
        if (!program)
        {
            return std::unexpected(program.error());
        }
        m_Diagnostics = program->diagnostics;
#ifdef VULTRA_WITH_SHADER_COMPILER
        if (m_Watch)
        {
            // Recurse the shader tree, not every third-party search root for every pipeline.
            auto directories = detail::shaderWatchDirectories(program->dependencies, m_File, m_CompileOptions);
            if (!directories)
            {
                return std::unexpected(directories.error());
            }
            m_Watch->dependencyDirectories = std::move(*directories);
            if (auto watched = m_Watch->refreshDirectories(); !watched)
            {
                return watched;
            }
        }
#endif
        return replace(*program);
    }

    std::expected<void, Error> ShaderPipeline::replace(const ShaderProgram& program)
    {
        const auto& desc = *m_Device.core.GetDeviceDesc(m_Device.handle);
        if (auto valid = detail::validateShaderProgram(program, desc); !valid)
        {
            return valid;
        }
        const auto shaders = program.descriptors(m_CompileOptions.entries);
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
        const auto replacement = m_Builder(*shaders);
        if (!replacement)
        {
            return std::unexpected(replacement.error());
        }
        if (!*replacement)
        {
            return makeError("Pipeline builder returned null");
        }
        m_Device.core.SetDebugName(*replacement, m_File.filename().string().c_str());
        if (auto idle = m_Device.waitIdle(); !idle)
        {
            // The replacement was never recorded, so it can go without waiting for the device.
            m_Device.core.DestroyPipeline(*replacement);
            return idle;
        }
        if (m_Pipeline)
        {
            m_Device.core.DestroyPipeline(m_Pipeline);
        }
        m_Pipeline = *replacement;
        m_Identity = std::move(identity);
        ++m_Generation;
        return {};
    }
} // namespace vultra
