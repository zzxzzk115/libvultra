#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>

#include <set>

namespace vultra::detail
{
    ShaderDebugInformation shaderDebugInformation(const ShaderCompileOptions& options);
    std::expected<std::filesystem::path, std::string> shaderAbsolutePathExpected(const std::filesystem::path& path);
    std::expected<bool, std::string>                  shaderRegularFileExpected(const std::filesystem::path& path);
    std::expected<std::string, std::string>           readShaderTextExpected(const std::filesystem::path& path);
    std::expected<std::string, std::string> shaderCompileSignatureExpected(const std::filesystem::path& source,
                                                                           std::string_view             text,
                                                                           const ShaderCompileOptions&  options);
    std::expected<bool, std::string> shaderDependenciesCurrentExpected(std::span<const ShaderDependency> dependencies,
                                                                       const std::filesystem::path&      source,
                                                                       const ShaderCompileOptions&       options);
    std::string            shaderCompileSignature(const std::filesystem::path& source,
                                                  std::string_view             text,
                                                  const ShaderCompileOptions&  options);
    std::string
    shaderCompileKey(const std::filesystem::path& source, std::string_view text, const ShaderCompileOptions& options);
    bool                            shaderDependenciesCurrent(std::span<const ShaderDependency> dependencies,
                                                              const std::filesystem::path&      source,
                                                              const ShaderCompileOptions&       options);
    std::expected<std::set<std::filesystem::path>, std::string>
                                    shaderWatchDirectoriesExpected(std::span<const ShaderDependency> dependencies,
                                                                   const std::filesystem::path&      source,
                                                                   const ShaderCompileOptions&       options);
    std::set<std::filesystem::path> shaderWatchDirectories(std::span<const ShaderDependency> dependencies,
                                                           const std::filesystem::path&      source,
                                                           const ShaderCompileOptions&       options);
} // namespace vultra::detail
