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
    // Copies the push-constant offsets of a SPIR-V layout of the same program into a D3D layout, where Slang lays
    // out [[vk::push_constant]] blocks as ordinary constant buffers.
    [[nodiscard]] std::expected<void, Error> markPushConstants(ShaderParameter& target, const ShaderParameter& spirv);
    [[nodiscard]] std::expected<std::vector<ShaderAttribute>, Error>
    reflectShaderAttributes(slang::FunctionReflection* function);
    // The keywords [VultraKeyword(name, type)] attributes declare, in order; a malformed one is an error naming it.
    [[nodiscard]] std::expected<std::vector<ShaderKeyword>, Error>
    shaderKeywords(std::span<const ShaderAttribute> attributes);
    // Every keyword's value for `selection` ({name, value}, the default for a keyword it leaves out), in declaration
    // order; a name the keywords do not declare, or a value a keyword does not list, is an error naming the choices.
    [[nodiscard]] std::expected<std::vector<ShaderDefine>, Error>
    keywordValues(std::span<const ShaderKeyword> keywords, std::span<const ShaderDefine> selection);
    // The macros that select `values`: NAME = the value's index, for every value but a keyword's first.
    std::vector<ShaderDefine>                          keywordDefines(std::span<const ShaderKeyword> keywords,
                                                                      std::span<const ShaderDefine>  values);
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
