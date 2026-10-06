#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>

#include <nlohmann/json.hpp>

namespace slang
{
    struct VariableLayoutReflection;
}

namespace vultra::detail
{
    ShaderParameter reflectShaderParameters(slang::VariableLayoutReflection* globals);
    nlohmann::json  encodeShaderProgram(const ShaderProgram& program);
    ShaderProgram   decodeShaderProgram(const nlohmann::json& document);
    nlohmann::json  readShaderArchive(const std::filesystem::path& file);
    nlohmann::json  readShaderArchive(std::span<const std::byte> bytes, const std::filesystem::path& file);
    void            writeShaderArchive(const std::filesystem::path& file, const nlohmann::json& document);
} // namespace vultra::detail
