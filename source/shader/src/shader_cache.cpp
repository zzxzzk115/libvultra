#include "shader_archive.hpp"
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
        using Json = nlohmann::json;

        std::expected<std::string, Error> readText(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                return makeError("Read shader dependency: {}", path.string());
            }
            return std::string {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }

        std::expected<std::filesystem::path, Error> canonicalPath(const std::filesystem::path& path)
        {
            std::error_code error;
            auto            result = std::filesystem::weakly_canonical(path, error);
            if (error)
            {
                return makeError("Resolve shader dependency {}: {}", path.string(), error.message());
            }
            return result;
        }

        bool isFile(const std::filesystem::path& path)
        {
            // An unreadable status counts as absent; the dependency then reads as changed.
            std::error_code error;
            return std::filesystem::is_regular_file(path, error);
        }

        // The include path relative to the search root it was found under, or empty outside that root.
        std::expected<std::filesystem::path, Error> relativeToRoot(const std::filesystem::path& original,
                                                                   const std::filesystem::path& root)
        {
            const auto canonicalRoot = canonicalPath(root);
            if (!canonicalRoot)
            {
                return canonicalRoot;
            }
            auto relative = original.lexically_relative(*canonicalRoot);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            {
                return std::filesystem::path {};
            }
            return relative;
        }

        std::expected<std::vector<std::filesystem::path>, Error> searchRoots(const std::filesystem::path& source,
                                                                             const ShaderCompileOptions&  options)
        {
            const auto file = absolutePath(source);
            if (!file)
            {
                return std::unexpected(file.error());
            }
            std::vector<std::filesystem::path> roots {file->parent_path()};
            for (const auto& root : options.includeDirectories)
            {
                const auto directory = absolutePath(root);
                if (!directory)
                {
                    return std::unexpected(directory.error());
                }
                roots.push_back(*directory);
            }
            for (const auto& module : options.linkModules)
            {
                const auto path = absolutePath(module.is_absolute() ? module : source.parent_path() / module);
                if (!path)
                {
                    return std::unexpected(path.error());
                }
                roots.push_back(path->parent_path());
            }
            return roots;
        }
    } // namespace

    std::expected<std::string, Error> shaderCompileSignature(const std::filesystem::path& source,
                                                             std::string_view             text,
                                                             const ShaderCompileOptions&  options)
    {
        const auto file = absolutePath(source);
        if (!file)
        {
            return std::unexpected(file.error());
        }
        Json key {{"compiler", ShaderProgram::compilerVersion()},
                  {"source", text},
                  {"path", file->lexically_normal().generic_string()},
                  {"profile", options.profile},
                  {"ray_query", options.rayQuery},
                  {"capabilities", options.capabilities},
                  {"debug_info", options.debugInfo},
                  {"optimize", options.optimize},
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
            const auto directory = absolutePath(root);
            if (!directory)
            {
                return std::unexpected(directory.error());
            }
            key["includes"].push_back(directory->lexically_normal().generic_string());
        }
        for (const auto& module : options.linkModules)
        {
            const auto path = absolutePath(module.is_absolute() ? module : source.parent_path() / module);
            if (!path)
            {
                return std::unexpected(path.error());
            }
            const auto contents = readText(*path);
            if (!contents)
            {
                return std::unexpected(contents.error());
            }
            key["modules"].push_back({path->lexically_normal().generic_string(), *contents});
        }
        for (const auto& define : options.defines)
        {
            key["defines"].push_back({define.name, define.value});
        }
        for (const auto& module : options.linkSources)
        {
            key["link_sources"].push_back({module.name, module.value});
        }
        // nlohmann rejects text that is not UTF-8 only by throwing from dump().
        try
        {
            return key.dump();
        }
        catch (const Json::type_error& error)
        {
            return makeError("Serialize shader compile request {}: {}", source.string(), error.what());
        }
    }

    std::expected<std::string, Error>
    shaderCompileKey(const std::filesystem::path& source, std::string_view text, const ShaderCompileOptions& options)
    {
        const auto serialized = shaderCompileSignature(source, text, options);
        if (!serialized)
        {
            return serialized;
        }
        return std::to_string(XXH3_64bits(serialized->data(), serialized->size()));
    }

    std::expected<bool, Error> shaderDependenciesCurrent(std::span<const ShaderDependency> dependencies,
                                                         const std::filesystem::path&      source,
                                                         const ShaderCompileOptions&       options)
    {
        const auto roots = searchRoots(source, options);
        if (!roots)
        {
            return std::unexpected(roots.error());
        }
        for (const auto& dependency : dependencies)
        {
            if (!isFile(dependency.path))
            {
                Logger::core().info("Shader dependency missing: {}", dependency.path.string());
                return false;
            }
            const auto contents = readText(dependency.path);
            if (!contents)
            {
                return std::unexpected(contents.error());
            }
            if (XXH3_64bits(contents->data(), contents->size()) != dependency.hash)
            {
                Logger::core().info("Shader dependency changed: {}", dependency.path.string());
                return false;
            }
            const auto original = canonicalPath(dependency.path);
            if (!original)
            {
                return std::unexpected(original.error());
            }
            for (const auto& oldRoot : *roots)
            {
                const auto relative = relativeToRoot(*original, oldRoot);
                if (!relative)
                {
                    return std::unexpected(relative.error());
                }
                if (relative->empty())
                {
                    continue;
                }
                for (const auto& root : *roots)
                {
                    const auto candidate = root / *relative;
                    if (isFile(candidate))
                    {
                        const auto resolved = canonicalPath(candidate);
                        if (!resolved)
                        {
                            return std::unexpected(resolved.error());
                        }
                        if (*resolved != *original)
                        {
                            Logger::core().info("Shader dependency resolution changed: {} -> {}",
                                                original->string(),
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

    std::expected<std::set<std::filesystem::path>, Error>
    shaderWatchDirectories(std::span<const ShaderDependency> dependencies,
                           const std::filesystem::path&      source,
                           const ShaderCompileOptions&       options)
    {
        const auto roots = searchRoots(source, options);
        if (!roots)
        {
            return std::unexpected(roots.error());
        }
        std::set<std::filesystem::path> directories;
        auto                            addDirectory = [&](std::filesystem::path directory)
        {
            // An unreadable status counts as missing, so the nearest readable parent is watched instead.
            std::error_code error;
            while (!directory.empty() && !std::filesystem::is_directory(directory, error))
            {
                directory = directory.parent_path();
            }
            if (!directory.empty())
            {
                directories.insert(directory.lexically_normal());
            }
        };
        for (const auto& root : *roots)
        {
            addDirectory(root);
        }
        for (const auto& dependency : dependencies)
        {
            addDirectory(dependency.path.parent_path());
            const auto original = canonicalPath(dependency.path);
            if (!original)
            {
                return std::unexpected(original.error());
            }
            for (const auto& oldRoot : *roots)
            {
                const auto relative = relativeToRoot(*original, oldRoot);
                if (!relative)
                {
                    return std::unexpected(relative.error());
                }
                if (relative->empty())
                {
                    continue;
                }
                // A newly created include in an earlier search root can replace the resolved dependency.
                // Watch its nearest existing parent so later directory creation also triggers a refresh.
                for (const auto& root : *roots)
                {
                    addDirectory((root / *relative).parent_path());
                }
            }
        }
        return directories;
    }
} // namespace vultra::detail
