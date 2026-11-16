#include "shader_cache.hpp"
#include "shader_toolchain_key.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/shader/shader_compiler.hpp>

#include <slang.h>

#include <algorithm>
#include <fstream>
#include <utility>
#include <vector>

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

        // Whether an existing cooked archive holds exactly these targets, built from the same requests (one key per
        // target) and the same inputs.
        std::expected<bool, Error> cookedProgramCurrent(const std::filesystem::path&                    source,
                                                        const std::filesystem::path&                    output,
                                                        std::span<const std::pair<std::string, std::string>> keys,
                                                        const ShaderCompileOptions&                     options)
        {
            const auto previous = ShaderProgram::loadTargets(output);
            if (!previous)
            {
                return std::unexpected(previous.error());
            }
            if (previous->size() != keys.size())
            {
                return false;
            }
            for (const auto& [profile, key] : keys)
            {
                const auto found = std::ranges::find(*previous, profile, &ShaderProgram::profile);
                if (found == previous->end() || found->compileKey != key)
                {
                    return false;
                }
                const auto current = detail::shaderDependenciesCurrent(found->dependencies, source, options);
                if (!current || !*current)
                {
                    return current;
                }
            }
            return true;
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
                                                   const ShaderCompileOptions&  options,
                                                   std::span<const std::string> profiles)
    {
        const auto text = readSource(source);
        if (!text)
        {
            return std::unexpected(text.error());
        }
        std::vector<ShaderCompileOptions> targets;
        for (const auto& profile : profiles)
        {
            targets.push_back(options);
            targets.back().profile = profile;
        }
        if (targets.empty())
        {
            targets.push_back(options);
        }
        std::vector<std::pair<std::string, std::string>> keys;
        for (const auto& target : targets)
        {
            const auto key = detail::shaderCompileKey(source, *text, target);
            if (!key)
            {
                return std::unexpected(key.error());
            }
            keys.emplace_back(target.profile, *key);
        }
        std::error_code error;
        if (std::filesystem::is_regular_file(output, error))
        {
            const auto current = cookedProgramCurrent(source, output, keys, options);
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
        std::vector<ShaderProgram> programs;
        for (const auto& target : targets)
        {
            auto candidate = compileSource(source, *text, target);
            if (!candidate)
            {
                return std::unexpected(candidate.error());
            }
            if (!candidate->diagnostics.empty())
            {
                Logger::core().warn("{}", candidate->diagnostics);
            }
            programs.push_back(std::move(*candidate));
        }
        if (auto saved = save(output, programs); !saved)
        {
            return std::unexpected(saved.error());
        }
        return true;
    }
} // namespace vultra
