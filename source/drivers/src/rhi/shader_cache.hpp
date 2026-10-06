#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>

#include <set>

namespace vultra::detail
{
    std::string shaderCompileSignature(const std::filesystem::path& source,
                                       std::string_view             text,
                                       const ShaderCompileOptions&  options);
    std::string
    shaderCompileKey(const std::filesystem::path& source, std::string_view text, const ShaderCompileOptions& options);
    bool                            shaderDependenciesCurrent(std::span<const ShaderDependency> dependencies,
                                                              const std::filesystem::path&      source,
                                                              const ShaderCompileOptions&       options);
    std::set<std::filesystem::path> shaderWatchDirectories(std::span<const ShaderDependency> dependencies,
                                                           const std::filesystem::path&      source,
                                                           const ShaderCompileOptions&       options);
} // namespace vultra::detail
