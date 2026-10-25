#include "shader_cache.hpp"
#include "shader_toolchain_key.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/shader_compiler.hpp>

#include <slang.h>

#include <fstream>
#include <stdexcept>

namespace vultra
{
    ShaderProgram ShaderProgram::compile(const std::filesystem::path&           file,
                                         std::span<const ShaderEntry>           requested,
                                         std::span<const std::filesystem::path> includeDirectories,
                                         bool                                   rayQuery)
    {
        ShaderCompileOptions options;
        options.entries.assign(requested.begin(), requested.end());
        options.includeDirectories.assign(includeDirectories.begin(), includeDirectories.end());
        options.rayQuery = rayQuery;
        return compile(file, options);
    }

    ShaderProgram ShaderProgram::compile(const std::filesystem::path& file, const ShaderCompileOptions& options)
    {
        std::ifstream input(file, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Open Slang source: " + file.string());
        }
        const std::string source {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        return compileSource(file, source, options);
    }

    ShaderProgram ShaderProgram::compileSource(const std::filesystem::path& file,
                                               std::string_view             text,
                                               const ShaderCompileOptions&  options)
    {
        ShaderCompiler compiler;
        return compiler.compileSource(file, text, options);
    }

    std::string ShaderProgram::compilerVersion()
    {
        std::string identity = std::string(spGetBuildTagString()) + ";" + kShaderToolchainKey;
#ifdef NDEBUG
        identity += ";debug=none";
#else
        identity += ";debug=standard";
#endif
        return identity;
    }

    bool ShaderProgram::cook(const std::filesystem::path& source,
                             const std::filesystem::path& output,
                             const ShaderCompileOptions&  options)
    {
        std::ifstream input(source, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Open Slang source: " + source.string());
        }
        const std::string text {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto        key = detail::shaderCompileKey(source, text, options);
        if (std::filesystem::is_regular_file(output))
        {
            try
            {
                const auto previous = load(output);
                if (previous.compileKey == key &&
                    detail::shaderDependenciesCurrent(previous.dependencies, source, options))
                {
                    Logger::core().info("Shader cache hit: {}", source.string());
                    return false;
                }
            }
            catch (const std::exception& error)
            {
                Logger::core().warn("Rebuilding unreadable shader cache {}: {}", output.string(), error.what());
            }
        }
        Logger::core().info("Cooking raw Slang shader: {}", source.string());
        auto candidate = compileSource(source, text, options);
        candidate.save(output);
        if (!candidate.diagnostics.empty())
        {
            Logger::core().warn("{}", candidate.diagnostics);
        }
        return true;
    }

} // namespace vultra
