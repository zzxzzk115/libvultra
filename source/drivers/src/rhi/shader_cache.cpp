#include "shader_cache.hpp"

#include <vultra/core/base/logger.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <fstream>
#include <stdexcept>

namespace vultra::detail
{
    std::expected<std::filesystem::path, std::string> shaderAbsolutePathExpected(const std::filesystem::path& path)
    {
        if (path.native().find(std::filesystem::path::value_type(0)) != std::filesystem::path::string_type::npos)
        {
            return std::unexpected("Shader path contains a NUL character");
        }
        std::error_code error;
        auto            absolute = std::filesystem::absolute(path, error);
        if (error)
        {
            return std::unexpected("Resolve shader path " + path.string() + ": " + error.message());
        }
        return absolute.lexically_normal();
    }

    std::expected<bool, std::string> shaderRegularFileExpected(const std::filesystem::path& path)
    {
        std::error_code error;
        const bool      regular = std::filesystem::is_regular_file(path, error);
        if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory)
        {
            return std::unexpected("Inspect shader file " + path.string() + ": " + error.message());
        }
        return regular;
    }

    std::expected<std::string, std::string> readShaderTextExpected(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        const auto    length = input ? input.tellg() : std::streampos(-1);
        if (length < std::streamoff(0) || length > std::streamoff(64 * 1024 * 1024))
        {
            return std::unexpected("Missing or invalid shader input: " + path.string());
        }
        std::string contents(static_cast<size_t>(length), char(0));
        input.seekg(0);
        if (!contents.empty())
        {
            input.read(contents.data(), std::streamsize(contents.size()));
        }
        if (!input || input.peek() != std::char_traits<char>::eof() || input.bad())
        {
            return std::unexpected("Read shader input failed or changed size: " + path.string());
        }
        return contents;
    }

    namespace
    {
        std::expected<std::filesystem::path, std::string> canonicalShaderPath(const std::filesystem::path& path)
        {
            std::error_code error;
            auto            canonical = std::filesystem::weakly_canonical(path, error);
            if (error)
            {
                return std::unexpected("Resolve shader dependency " + path.string() + ": " + error.message());
            }
            return canonical;
        }

        std::expected<std::vector<std::filesystem::path>, std::string>
        searchRootsExpected(const std::filesystem::path& source, const ShaderCompileOptions& options)
        {
            auto absolute = shaderAbsolutePathExpected(source);
            if (!absolute)
            {
                return std::unexpected(absolute.error());
            }
            std::vector<std::filesystem::path> roots {absolute->parent_path()};
            for (const auto& root : options.includeDirectories)
            {
                auto resolved = shaderAbsolutePathExpected(root);
                if (!resolved)
                {
                    return std::unexpected(resolved.error());
                }
                roots.push_back(std::move(*resolved));
            }
            for (const auto& module : options.linkModules)
            {
                auto resolved =
                    shaderAbsolutePathExpected(module.is_absolute() ? module : source.parent_path() / module);
                if (!resolved)
                {
                    return std::unexpected(resolved.error());
                }
                roots.push_back(resolved->parent_path());
            }
            return roots;
        }

        // The file-watcher boundary is still legacy; compiler/cache callers use the Expected function directly.
    } // namespace

    ShaderDebugInformation shaderDebugInformation(const ShaderCompileOptions& options)
    {
#ifdef NDEBUG
        constexpr auto buildDefault = ShaderDebugInformation::eNone;
#else
        constexpr auto buildDefault = ShaderDebugInformation::eStandard;
#endif
        return options.debugInformation.value_or(buildDefault);
    }

    std::expected<std::string, std::string> shaderCompileSignatureExpected(const std::filesystem::path& source,
                                                                           std::string_view             text,
                                                                           const ShaderCompileOptions&  options)
    {
        auto absolute = shaderAbsolutePathExpected(source);
        if (!absolute)
        {
            return std::unexpected(absolute.error());
        }
        using Json = nlohmann::json;
        Json key {{"compiler", ShaderProgram::compilerVersion()},
                  {"source", text},
                  {"path", absolute->generic_string()},
                  {"profile", options.profile},
                  {"ray_query", options.rayQuery},
                  {"optimization", options.optimization},
                  {"debug_information", shaderDebugInformation(options)},
                  {"capabilities", options.capabilities},
                  {"entries", Json::array()},
                  {"includes", Json::array()},
                  {"modules", Json::array()},
                  {"defines", Json::array()},
                  {"link_sources", Json::array()}};
        if (!options.runSpirvToolsOptimization)
        {
            key["spirv_tools_optimization"] = false;
        }
        for (const auto& entry : options.entries)
        {
            key["entries"].push_back({entry.name, uint32_t(entry.stage)});
        }
        for (const auto& root : options.includeDirectories)
        {
            auto path = shaderAbsolutePathExpected(root);
            if (!path)
            {
                return std::unexpected(path.error());
            }
            key["includes"].push_back(path->generic_string());
        }
        for (const auto& module : options.linkModules)
        {
            auto path = shaderAbsolutePathExpected(module.is_absolute() ? module : source.parent_path() / module);
            if (!path)
            {
                return std::unexpected(path.error());
            }
            auto contents = readShaderTextExpected(*path);
            if (!contents)
            {
                return std::unexpected(contents.error());
            }
            if (contents->find(char(0)) != std::string::npos)
            {
                return std::unexpected("Linked shader input contains a NUL character: " + path->string());
            }
            key["modules"].push_back({path->generic_string(), std::move(*contents)});
        }
        for (const auto& define : options.defines)
        {
            key["defines"].push_back({define.name, define.value});
        }
        for (const auto& module : options.linkSources)
        {
            key["link_sources"].push_back({module.name, module.value});
        }
        // Conformances affect linking, not frontend IR. Empty selections add no request field.
        if (!options.typeConformances.empty())
        {
            if (auto valid = ShaderTypeConformance::validateExpected(options.typeConformances); !valid)
            {
                return std::unexpected(valid.error());
            }
            auto&                                         conformances = key["type_conformances"] = Json::array();
            for (const auto& conformance : options.typeConformances)
            {
                conformances.push_back({conformance.typeName, conformance.interfaceType, conformance.id});
            }
        }
        // Nonthrowing serialization must not silently replace malformed UTF-8 and merge distinct requests.
        const auto serialized = key.dump(-1, ' ', false, Json::error_handler_t::replace);
        if (Json::parse(serialized, nullptr, false) != key)
        {
            return std::unexpected("Shader request metadata must contain valid UTF-8");
        }
        return serialized;
    }

    std::string shaderCompileSignature(const std::filesystem::path& source,
                                       std::string_view             text,
                                       const ShaderCompileOptions&  options)
    {
        auto signature = shaderCompileSignatureExpected(source, text, options);
        if (!signature)
        {
            throw std::runtime_error(signature.error());
        }
        return std::move(*signature);
    }

    std::string
    shaderCompileKey(const std::filesystem::path& source, std::string_view text, const ShaderCompileOptions& options)
    {
        const auto serialized = shaderCompileSignature(source, text, options);
        return std::to_string(XXH3_64bits(serialized.data(), serialized.size()));
    }

    std::expected<bool, std::string> shaderDependenciesCurrentExpected(std::span<const ShaderDependency> dependencies,
                                                                       const std::filesystem::path&      source,
                                                                       const ShaderCompileOptions&       options)
    {
        auto roots = searchRootsExpected(source, options);
        if (!roots)
        {
            return std::unexpected(roots.error());
        }
        for (const auto& dependency : dependencies)
        {
            auto regular = shaderRegularFileExpected(dependency.path);
            if (!regular)
            {
                return std::unexpected(regular.error());
            }
            if (!*regular)
            {
                Logger::core().info("Shader dependency missing: {}", dependency.path.string());
                return false;
            }
            auto contents = readShaderTextExpected(dependency.path);
            if (!contents)
            {
                return std::unexpected(contents.error());
            }
            if (XXH3_64bits(contents->data(), contents->size()) != dependency.hash)
            {
                Logger::core().info("Shader dependency changed: {}", dependency.path.string());
                return false;
            }
            auto original = canonicalShaderPath(dependency.path);
            if (!original)
            {
                return std::unexpected(original.error());
            }
            for (const auto& oldRoot : *roots)
            {
                auto canonicalRoot = canonicalShaderPath(oldRoot);
                if (!canonicalRoot)
                {
                    return std::unexpected(canonicalRoot.error());
                }
                const auto relative = original->lexically_relative(*canonicalRoot);
                if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                {
                    continue;
                }
                for (const auto& root : *roots)
                {
                    const auto candidate = root / relative;
                    auto       exists    = shaderRegularFileExpected(candidate);
                    if (!exists)
                    {
                        return std::unexpected(exists.error());
                    }
                    if (*exists)
                    {
                        auto canonical = canonicalShaderPath(candidate);
                        if (!canonical)
                        {
                            return std::unexpected(canonical.error());
                        }
                        if (*canonical != *original)
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

    bool shaderDependenciesCurrent(std::span<const ShaderDependency> dependencies,
                                   const std::filesystem::path&      source,
                                   const ShaderCompileOptions&       options)
    {
        auto current = shaderDependenciesCurrentExpected(dependencies, source, options);
        if (!current)
        {
            throw std::runtime_error(current.error());
        }
        return *current;
    }

    std::expected<std::set<std::filesystem::path>, std::string>
    shaderWatchDirectoriesExpected(std::span<const ShaderDependency> dependencies,
                                   const std::filesystem::path&      source,
                                   const ShaderCompileOptions&       options)
    {
        auto roots = searchRootsExpected(source, options);
        if (!roots)
        {
            return std::unexpected(roots.error());
        }
        std::set<std::filesystem::path> directories;
        const auto addDirectory = [&](std::filesystem::path directory) -> std::expected<void, std::string>
        {
            while (!directory.empty())
            {
                std::error_code error;
                if (std::filesystem::is_directory(directory, error))
                {
                    directories.insert(directory.lexically_normal());
                    return {};
                }
                if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory)
                {
                    return std::unexpected("Inspect shader watch directory: " + error.message());
                }
                auto parent = directory.parent_path();
                if (parent == directory)
                {
                    break;
                }
                directory = std::move(parent);
            }
            return {};
        };
        for (const auto& root : *roots)
        {
            if (auto added = addDirectory(root); !added)
            {
                return std::unexpected(added.error());
            }
        }
        for (const auto& dependency : dependencies)
        {
            if (auto added = addDirectory(dependency.path.parent_path()); !added)
            {
                return std::unexpected(added.error());
            }
            std::error_code error;
            const auto      original = std::filesystem::weakly_canonical(dependency.path, error);
            if (error)
            {
                return std::unexpected("Resolve shader dependency watch path: " + error.message());
            }
            for (const auto& oldRoot : *roots)
            {
                const auto canonicalRoot = std::filesystem::weakly_canonical(oldRoot, error);
                if (error)
                {
                    return std::unexpected("Resolve shader search root: " + error.message());
                }
                const auto relative = original.lexically_relative(canonicalRoot);
                if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                {
                    continue;
                }
                // A new include in an earlier search root can replace the currently resolved dependency.
                for (const auto& root : *roots)
                {
                    if (auto added = addDirectory((root / relative).parent_path()); !added)
                    {
                        return std::unexpected(added.error());
                    }
                }
            }
        }
        return directories;
    }

    std::set<std::filesystem::path> shaderWatchDirectories(std::span<const ShaderDependency> dependencies,
                                                           const std::filesystem::path&      source,
                                                           const ShaderCompileOptions&       options)
    {
        auto directories = shaderWatchDirectoriesExpected(dependencies, source, options);
        if (!directories)
        {
            throw std::runtime_error(directories.error());
        }
        return std::move(*directories);
    }
} // namespace vultra::detail
