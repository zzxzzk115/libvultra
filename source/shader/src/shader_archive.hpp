#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/shader/shader_program.hpp>

#include <nlohmann/json.hpp>

#include <expected>

namespace slang
{
    struct VariableLayoutReflection;
    struct FunctionReflection;
} // namespace slang

namespace vultra::detail
{
    [[nodiscard]] std::expected<void, Error> validateShaderProgram(const ShaderProgram& program);
    [[nodiscard]] std::expected<void, Error> validateShaderProgram(const ShaderProgram& program,
                                                                   const VriDeviceDesc& device);
    [[nodiscard]] std::expected<ShaderParameter, Error>
    reflectShaderParameters(slang::VariableLayoutReflection* globals);
    [[nodiscard]] std::expected<std::vector<ShaderAttribute>, Error>
                                                       reflectShaderAttributes(slang::FunctionReflection* function);
    nlohmann::json                                     encodeShaderProgram(const ShaderProgram& program);
    [[nodiscard]] std::expected<ShaderProgram, Error>  decodeShaderProgram(const nlohmann::json& document);
    [[nodiscard]] std::expected<nlohmann::json, Error> readShaderArchive(const std::filesystem::path& file);
    [[nodiscard]] std::expected<nlohmann::json, Error> readShaderArchive(std::span<const std::byte>   bytes,
                                                                         const std::filesystem::path& file);
    [[nodiscard]] std::expected<void, Error>           writeShaderArchive(const std::filesystem::path& file,
                                                                          const nlohmann::json&        document);
    // std::filesystem::absolute, reporting a failed current-directory query instead of throwing.
    [[nodiscard]] std::expected<std::filesystem::path, Error> absolutePath(const std::filesystem::path& path);
} // namespace vultra::detail
