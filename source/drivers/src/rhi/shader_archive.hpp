#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>

#include <nlohmann/json.hpp>

namespace slang
{
    struct ShaderReflection;
}

namespace vultra::detail
{
    nlohmann::json                              encodeShaderParameter(const ShaderParameter& parameter);
    std::expected<ShaderParameter, std::string> decodeShaderParameterExpected(const nlohmann::json& document);
    void            validateShaderProgram(const ShaderProgram& program);
    void            validateShaderProgram(const ShaderProgram& program, const VriDeviceDesc& device);
    std::expected<void, std::string> validateShaderProgramExpected(const ShaderProgram& program);
    std::expected<void, std::string> validateShaderProgramExpected(const ShaderProgram& program, const VriDeviceDesc& device);
    std::expected<ShaderParameter, std::string> reflectShaderParametersExpected(slang::ShaderReflection* program);
    nlohmann::json  encodeShaderProgram(const ShaderProgram& program);
    ShaderProgram   decodeShaderProgram(const nlohmann::json& document);
    std::expected<ShaderProgram, std::string> decodeShaderProgramExpected(const nlohmann::json& document);
    std::expected<nlohmann::json, std::string> readShaderArchiveExpected(const std::filesystem::path& file);
    std::expected<nlohmann::json, std::string> readShaderArchiveExpected(std::span<const std::byte>   bytes,
                                                                         const std::filesystem::path& file);
    std::expected<void, std::string>           writeShaderArchiveExpected(const std::filesystem::path& file,
                                                                          const nlohmann::json&        document);
    nlohmann::json  readShaderArchive(const std::filesystem::path& file);
    nlohmann::json  readShaderArchive(std::span<const std::byte> bytes, const std::filesystem::path& file);
    void            writeShaderArchive(const std::filesystem::path& file, const nlohmann::json& document);
} // namespace vultra::detail
