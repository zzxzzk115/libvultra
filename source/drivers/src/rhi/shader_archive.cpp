#include "shader_archive.hpp"

#include <vultra/platform/os/file.hpp>

#include <xxhash.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace vultra::detail
{
    namespace
    {
        constexpr std::array<uint8_t, 8> kMagic {'V', 'U', 'L', 'T', 'R', 'A', 'S', 'H'};
        constexpr size_t                 kHeaderSize  = 16;
        constexpr size_t                 kMaxFileSize = 64 * 1024 * 1024;
        using Json                                    = nlohmann::json;

    } // namespace

    Json encodeShaderParameter(const ShaderParameter& parameter)
    {
        Json offsets = Json::array();
        for (const auto& offset : parameter.offsets)
        {
            offsets.push_back({{"kind", offset.kind}, {"offset", offset.offset}, {"space", offset.space}});
        }
        Json fields = Json::array();
        for (const auto& field : parameter.fields)
        {
            fields.push_back(encodeShaderParameter(field));
        }
        return {{"name", parameter.name},
                {"type", parameter.typeName},
                {"kind", parameter.kind},
                {"scalar", parameter.scalar},
                {"size", parameter.size},
                {"stride", parameter.stride},
                {"count", parameter.count},
                {"matrix_stride", parameter.matrixStride},
                {"rows", parameter.rows},
                {"columns", parameter.columns},
                {"column_major", parameter.columnMajor},
                {"descriptor_type", parameter.descriptorType ? Json(*parameter.descriptorType) : Json()},
                {"view_type", parameter.viewType},
                {"offsets", offsets},
                {"fields", fields}};
    }

    Json encodeShaderProgram(const ShaderProgram& program)
    {
        Json document {{"target", program.profile},
                       {"compile_key", program.compileKey},
                       {"capabilities", program.capabilities},
                       {"ray_query", program.rayQuery},
                       {"entries", Json::array()},
                       {"parameters", encodeShaderParameter(program.parameters)},
                       {"dependencies", Json::array()}};
        for (const auto& shader : program.shaders)
        {
            const auto           bytes = program.profile == "wgsl" ?
                                             std::as_bytes(std::span(shader.wgsl.data(), shader.wgsl.size())) :
                                             std::as_bytes(std::span(shader.words));
            std::vector<uint8_t> code(bytes.size());
            std::memcpy(code.data(), bytes.data(), bytes.size());
            document["entries"].push_back({{"name", shader.entry.name},
                                           {"stage", uint32_t(shader.entry.stage)},
                                           {"color_outputs", shader.colorOutputs},
                                           {"thread_group", shader.threadGroup},
                                           {"code", Json::binary(std::move(code))}});
        }
        for (const auto& dependency : program.dependencies)
        {
            document["dependencies"].push_back({{"path", dependency.path.generic_string()}, {"hash", dependency.hash}});
        }
        return document;
    }

    ShaderProgram decodeShaderProgram(const Json& document)
    {
        auto program = decodeShaderProgramExpected(document);
        if (!program)
        {
            throw std::runtime_error(program.error());
        }
        return std::move(*program);
    }

    std::expected<void, std::string> writeShaderArchiveExpected(const std::filesystem::path& file, const Json& document)
    {
        static_assert(std::endian::native == std::endian::little);
        if (!document.is_object())
        {
            return std::unexpected("Shader archive must be an object");
        }
        const auto kind    = document.find("kind");
        const auto version = document.find("version");
        if (kind == document.end() || !kind->is_string() || version == document.end() ||
            !version->is_number_integer() || *version != 1 ||
            (*kind != "raw_slang" && *kind != "game_shader" && *kind != "development_shader"))
        {
            return std::unexpected("Unsupported shader archive format");
        }
        const auto extension = *kind == "development_shader" ? ".vshadercache" : ".vshaderc";
        if (file.extension() != extension)
        {
            return std::unexpected(std::string("Shader output must use ") + extension);
        }
        const auto payload = Json::to_cbor(document);
        if (payload.size() > kMaxFileSize - kHeaderSize)
        {
            return std::unexpected("Cooked shader exceeds 64 MiB");
        }
        std::vector<uint8_t> bytes(kHeaderSize);
        std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
        const auto hash = XXH3_64bits(payload.data(), payload.size());
        for (size_t index = 0; index < sizeof(hash); ++index)
        {
            bytes[8 + index] = uint8_t(hash >> (8 * index));
        }
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        if (!file.parent_path().empty())
        {
            std::error_code error;
            std::filesystem::create_directories(file.parent_path(), error);
            if (error)
            {
                return std::unexpected("Create shader archive directory: " + error.message());
            }
        }
        return writeFileAtomicallyExpected(file, std::as_bytes(std::span(bytes)));
    }

    void writeShaderArchive(const std::filesystem::path& file, const Json& document)
    {
        if (auto result = writeShaderArchiveExpected(file, document); !result)
        {
            throw std::runtime_error(result.error());
        }
    }

    Json readShaderArchive(const std::filesystem::path& file)
    {
        auto document = readShaderArchiveExpected(file);
        if (!document)
        {
            throw std::runtime_error(document.error());
        }
        return std::move(*document);
    }

    Json readShaderArchive(std::span<const std::byte> data, const std::filesystem::path& file)
    {
        auto document = readShaderArchiveExpected(data, file);
        if (!document)
        {
            throw std::runtime_error(document.error());
        }
        return std::move(*document);
    }
} // namespace vultra::detail
