#pragma once

#include <vultra/shader/shader_program.hpp>

#include <nlohmann/json.hpp>

namespace slang
{
    struct VariableLayoutReflection;
    struct FunctionReflection;
} // namespace slang

namespace vultra::detail
{
    void            validateShaderProgram(const ShaderProgram& program);
    void            validateShaderProgram(const ShaderProgram& program, const VriDeviceDesc& device);
    ShaderParameter reflectShaderParameters(slang::VariableLayoutReflection* globals);
    std::vector<ShaderAttribute> reflectShaderAttributes(slang::FunctionReflection* function);
    nlohmann::json  encodeShaderProgram(const ShaderProgram& program);
    ShaderProgram   decodeShaderProgram(const nlohmann::json& document);
    nlohmann::json  readShaderArchive(const std::filesystem::path& file);
    nlohmann::json  readShaderArchive(std::span<const std::byte> bytes, const std::filesystem::path& file);
    void            writeShaderArchive(const std::filesystem::path& file, const nlohmann::json& document);
} // namespace vultra::detail
