#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/shader/shader_program.hpp>

#include <expected>
#include <set>

namespace vultra::detail
{
    [[nodiscard]] std::expected<std::string, Error> shaderCompileSignature(const std::filesystem::path& source,
                                                                           std::string_view             text,
                                                                           const ShaderCompileOptions&  options);
    [[nodiscard]] std::expected<std::string, Error>
    shaderCompileKey(const std::filesystem::path& source, std::string_view text, const ShaderCompileOptions& options);
    [[nodiscard]] std::expected<bool, Error> shaderDependenciesCurrent(std::span<const ShaderDependency> dependencies,
                                                                       const std::filesystem::path&      source,
                                                                       const ShaderCompileOptions&       options);
    [[nodiscard]] std::expected<std::set<std::filesystem::path>, Error>
    shaderWatchDirectories(std::span<const ShaderDependency> dependencies,
                           const std::filesystem::path&      source,
                           const ShaderCompileOptions&       options);
} // namespace vultra::detail
