#include "shader_cache.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <fstream>

namespace vultra::detail
{
    namespace
    {
        std::string readText(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error("Read shader dependency: " + path.string());
            }
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }
    } // namespace

    std::string
    shaderCompileKey(const std::filesystem::path& source, std::string_view text, const ShaderCompileOptions& options)
    {
        using Json = nlohmann::json;
        Json key {{"compiler", ShaderProgram::compilerVersion()},
                  {"source", text},
                  {"path", std::filesystem::absolute(source).lexically_normal().generic_string()},
                  {"profile", options.profile},
                  {"ray_query", options.rayQuery},
                  {"capabilities", options.capabilities},
                  {"entries", Json::array()},
                  {"includes", Json::array()},
                  {"modules", Json::array()},
                  {"defines", Json::array()},
                  {"link_sources", Json::array()}};
        for (const auto& entry : options.entries)
        {
            key["entries"].push_back({entry.name, uint32_t(entry.stage)});
        }
        for (const auto& root : options.includeDirectories)
        {
            key["includes"].push_back(std::filesystem::absolute(root).lexically_normal().generic_string());
        }
        for (const auto& module : options.linkModules)
        {
            const auto path = std::filesystem::absolute(module.is_absolute() ? module : source.parent_path() / module);
            key["modules"].push_back({path.lexically_normal().generic_string(), readText(path)});
        }
        for (const auto& define : options.defines)
        {
            key["defines"].push_back({define.name, define.value});
        }
        for (const auto& module : options.linkSources)
        {
            key["link_sources"].push_back({module.name, module.value});
        }
        const auto serialized = key.dump();
        return std::to_string(XXH3_64bits(serialized.data(), serialized.size()));
    }

    bool shaderDependenciesCurrent(std::span<const ShaderDependency> dependencies,
                                   const std::filesystem::path&      source,
                                   const ShaderCompileOptions&       options)
    {
        std::vector<std::filesystem::path> roots {std::filesystem::absolute(source).parent_path()};
        for (const auto& root : options.includeDirectories)
        {
            roots.push_back(std::filesystem::absolute(root));
        }
        for (const auto& module : options.linkModules)
        {
            roots.push_back(
                std::filesystem::absolute(module.is_absolute() ? module : source.parent_path() / module).parent_path());
        }
        for (const auto& dependency : dependencies)
        {
            if (!std::filesystem::is_regular_file(dependency.path))
            {
                Logger::core().info("Shader dependency missing: {}", dependency.path.string());
                return false;
            }
            const auto contents = readText(dependency.path);
            if (XXH3_64bits(contents.data(), contents.size()) != dependency.hash)
            {
                Logger::core().info("Shader dependency changed: {}", dependency.path.string());
                return false;
            }
            const auto original = std::filesystem::weakly_canonical(dependency.path);
            for (const auto& oldRoot : roots)
            {
                const auto relative = original.lexically_relative(std::filesystem::weakly_canonical(oldRoot));
                if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                {
                    continue;
                }
                for (const auto& root : roots)
                {
                    const auto candidate = root / relative;
                    if (std::filesystem::is_regular_file(candidate))
                    {
                        if (std::filesystem::weakly_canonical(candidate) != original)
                        {
                            Logger::core().info("Shader dependency resolution changed: {} -> {}",
                                                original.string(),
                                                candidate.string());
                            return false;
                        }
                        break;
                    }
                }
            }
        }
        return true;
    }
} // namespace vultra::detail
