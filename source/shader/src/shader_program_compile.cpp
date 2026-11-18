#include "shader_archive.hpp"
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

        // Whether an existing cooked archive holds the variants of exactly these targets, built from the same requests
        // and the same inputs. Each target's default variant has the key of `targets`; the others add their keyword
        // macros, so their keys follow from the keywords the archive records.
        std::expected<bool, Error> cookedProgramCurrent(const std::filesystem::path&          source,
                                                        std::string_view                      text,
                                                        const std::filesystem::path&          output,
                                                        std::span<const ShaderCompileOptions> targets)
        {
            const auto previous = ShaderProgram::loadTargets(output);
            if (!previous)
            {
                return std::unexpected(previous.error());
            }
            size_t expected = 0;
            for (const auto& target : targets)
            {
                const auto first = std::ranges::find(*previous, target.profile, &ShaderProgram::profile);
                if (first == previous->end())
                {
                    return false;
                }
                size_t combinations = 1;
                for (const auto& keyword : first->keywords)
                {
                    combinations *= keyword.values.size();
                }
                expected += combinations;
                for (const auto& program : *previous)
                {
                    if (program.profile != target.profile)
                    {
                        continue;
                    }
                    auto       variant = target;
                    const auto defines = detail::keywordDefines(program.keywords, program.keywordValues);
                    variant.defines.insert(variant.defines.end(), defines.begin(), defines.end());
                    const auto key = detail::shaderCompileKey(source, text, variant);
                    if (!key)
                    {
                        return std::unexpected(key.error());
                    }
                    if (program.compileKey != *key)
                    {
                        return false;
                    }
                    const auto current = detail::shaderDependenciesCurrent(program.dependencies, source, variant);
                    if (!current || !*current)
                    {
                        return current;
                    }
                }
            }
            return previous->size() == expected;
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

    std::expected<std::vector<ShaderProgram>, Error> ShaderProgram::compileVariants(const std::filesystem::path& file,
                                                                                    const ShaderCompileOptions& options)
    {
        auto compiler = ShaderCompiler::create();
        if (!compiler)
        {
            return std::unexpected(compiler.error());
        }
        return compiler->compileVariants(file, options);
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
        std::error_code error;
        if (std::filesystem::is_regular_file(output, error))
        {
            const auto current = cookedProgramCurrent(source, *text, output, targets);
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
        auto compiler = ShaderCompiler::create();
        if (!compiler)
        {
            return std::unexpected(compiler.error());
        }
        std::vector<ShaderProgram> programs;
        for (const auto& target : targets)
        {
            auto variants = compiler->compileVariants(source, *text, target);
            if (!variants)
            {
                return std::unexpected(variants.error());
            }
            for (auto& variant : *variants)
            {
                if (!variant.diagnostics.empty())
                {
                    Logger::core().warn("{}", variant.diagnostics);
                }
                programs.push_back(std::move(variant));
            }
        }
        if (auto saved = save(output, programs); !saved)
        {
            return std::unexpected(saved.error());
        }
        return true;
    }
} // namespace vultra
