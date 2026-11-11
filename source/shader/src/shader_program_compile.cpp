#include "shader_cache.hpp"
#include "shader_toolchain_key.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/shader/shader_compiler.hpp>

#include <slang.h>

#include <fstream>

namespace vultra
{
    namespace
    {
        std::expected<std::string, Error> readSource(const std::filesystem::path& file)
        {
            std::ifstream input(file, std::ios::binary);
            if (!input)
            {
                return makeError("Open Slang source: {}", file.string());
            }
            return std::string {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }

        // Whether an existing cooked program was built from the same request and the same inputs.
        std::expected<bool, Error> cookedProgramCurrent(const std::filesystem::path& source,
                                                        const std::filesystem::path& output,
                                                        const std::string&           key,
                                                        const ShaderCompileOptions&  options)
        {
            const auto previous = ShaderProgram::load(output);
            if (!previous)
            {
                return std::unexpected(previous.error());
            }
            if (previous->compileKey != key)
            {
                return false;
            }
            return detail::shaderDependenciesCurrent(previous->dependencies, source, options);
        }
    } // namespace

    std::expected<ShaderProgram, Error>
    ShaderProgram::compile(const std::filesystem::path&           file,
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

    std::expected<ShaderProgram, Error> ShaderProgram::compile(const std::filesystem::path& file,
                                                               const ShaderCompileOptions&  options)
    {
        const auto source = readSource(file);
        if (!source)
        {
            return std::unexpected(source.error());
        }
        return compileSource(file, *source, options);
    }

    std::expected<ShaderProgram, Error> ShaderProgram::compileSource(const std::filesystem::path& file,
                                                                     std::string_view             text,
                                                                     const ShaderCompileOptions&  options)
    {
        auto compiler = ShaderCompiler::create();
        if (!compiler)
        {
            return std::unexpected(compiler.error());
        }
        return compiler->compileSource(file, text, options);
    }

    std::string ShaderProgram::compilerVersion()
    {
        return std::string(spGetBuildTagString()) + ";" + kShaderToolchainKey;
    }

    std::expected<bool, Error> ShaderProgram::cook(const std::filesystem::path& source,
                                                   const std::filesystem::path& output,
                                                   const ShaderCompileOptions&  options)
    {
        const auto text = readSource(source);
        if (!text)
        {
            return std::unexpected(text.error());
        }
        const auto key = detail::shaderCompileKey(source, *text, options);
        if (!key)
        {
            return std::unexpected(key.error());
        }
        std::error_code error;
        if (std::filesystem::is_regular_file(output, error))
        {
            const auto current = cookedProgramCurrent(source, output, *key, options);
            if (!current)
            {
                Logger::core().warn("Rebuilding unreadable shader cache {}: {}",
                                    output.string(),
                                    current.error().message);
            }
            else if (*current)
            {
                Logger::core().info("Shader cache hit: {}", source.string());
                return false;
            }
        }
        Logger::core().info("Cooking raw Slang shader: {}", source.string());
        auto candidate = compileSource(source, *text, options);
        if (!candidate)
        {
            return std::unexpected(candidate.error());
        }
        if (auto saved = candidate->save(output); !saved)
        {
            return std::unexpected(saved.error());
        }
        if (!candidate->diagnostics.empty())
        {
            Logger::core().warn("{}", candidate->diagnostics);
        }
        return true;
    }
} // namespace vultra
