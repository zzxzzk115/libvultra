#include "shader_archive.hpp"

#include <vultra/platform/os/file.hpp>

#include <xxhash.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>

namespace vultra::detail
{
    namespace
    {
        constexpr std::array<uint8_t, 8> kMagic {'V', 'U', 'L', 'T', 'R', 'A', 'S', 'H'};
        constexpr size_t                 kHeaderSize  = 16;
        constexpr size_t                 kMaxFileSize = 64 * 1024 * 1024;
        using Json                                    = nlohmann::json;

        Json encodeParameter(const ShaderParameter& parameter)
        {
            Json offsets = Json::array();
            for (const auto& offset : parameter.offsets)
            {
                offsets.push_back({{"kind", offset.kind}, {"offset", offset.offset}, {"space", offset.space}});
            }
            Json fields = Json::array();
            for (const auto& field : parameter.fields)
            {
                fields.push_back(encodeParameter(field));
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
                    {"shape", parameter.resourceShape},
                    {"access", parameter.resourceAccess},
                    {"offsets", offsets},
                    {"fields", fields}};
        }

        ShaderParameter decodeParameter(const Json& document, uint32_t depth, uint32_t& nodes)
        {
            if (depth > 64 || ++nodes > 100000)
            {
                throw std::invalid_argument("Excessively large shader parameter layout");
            }
            ShaderParameter parameter;
            parameter.name     = document.at("name").get<std::string>();
            parameter.typeName = document.at("type").get<std::string>();
            const auto kind    = document.at("kind").get<uint32_t>();
            const auto scalar  = document.at("scalar").get<uint32_t>();
            if (kind > uint32_t(ShaderParameterKind::eSampler) || scalar > uint32_t(ShaderScalarType::eOther) ||
                parameter.name.size() > 4096 || parameter.typeName.size() > 4096)
            {
                throw std::invalid_argument("Invalid cooked shader parameter type");
            }
            parameter.kind           = ShaderParameterKind(kind);
            parameter.scalar         = ShaderScalarType(scalar);
            parameter.size           = document.at("size").get<uint64_t>();
            parameter.stride         = document.at("stride").get<uint64_t>();
            parameter.count          = document.at("count").get<uint64_t>();
            parameter.matrixStride   = document.at("matrix_stride").get<uint64_t>();
            parameter.rows           = document.at("rows").get<uint32_t>();
            parameter.columns        = document.at("columns").get<uint32_t>();
            parameter.columnMajor    = document.at("column_major").get<bool>();
            parameter.resourceShape  = document.at("shape").get<uint32_t>();
            parameter.resourceAccess = document.at("access").get<uint32_t>();
            if (!document.at("offsets").is_array() || !document.at("fields").is_array())
            {
                throw std::invalid_argument("Invalid cooked shader layout children");
            }
            std::set<uint32_t> offsetKinds;
            for (const auto& offset : document.at("offsets"))
            {
                const auto offsetKind = offset.at("kind").get<uint32_t>();
                if (offsetKind > uint32_t(ShaderOffsetKind::ePushConstant) || !offsetKinds.insert(offsetKind).second)
                {
                    throw std::invalid_argument("Invalid cooked shader layout offset");
                }
                parameter.offsets.push_back({ShaderOffsetKind(offsetKind),
                                             offset.at("offset").get<uint64_t>(),
                                             offset.at("space").get<uint64_t>()});
            }
            for (const auto& field : document.at("fields"))
            {
                parameter.fields.push_back(decodeParameter(field, depth + 1, nodes));
            }
            return parameter;
        }

        bool validStage(uint32_t stage)
        {
            return stage == VriShaderStage_Vertex || stage == VriShaderStage_Fragment ||
                   stage == VriShaderStage_Compute || stage == VriShaderStage_Geometry ||
                   stage == VriShaderStage_TessControl || stage == VriShaderStage_TessEval ||
                   stage == VriShaderStage_Task || stage == VriShaderStage_Mesh;
        }
    } // namespace

    Json encodeShaderProgram(const ShaderProgram& program)
    {
        Json document {{"target", program.profile},
                       {"compile_key", program.compileKey},
                       {"capabilities", program.capabilities},
                       {"ray_query", program.rayQuery},
                       {"entries", Json::array()},
                       {"parameters", encodeParameter(program.parameters)},
                       {"dependencies", Json::array()}};
        for (const auto& shader : program.shaders)
        {
            const auto           bytes = std::as_bytes(std::span(shader.words));
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
        ShaderProgram program;
        program.profile      = document.at("target").get<std::string>();
        program.compileKey   = document.at("compile_key").get<std::string>();
        program.capabilities = document.at("capabilities").get<std::vector<std::string>>();
        if (program.compileKey.size() > 128 || program.capabilities.size() > 128)
        {
            throw std::invalid_argument("Invalid cooked shader compiler metadata");
        }
        if (!program.profile.starts_with("spirv_") || program.profile.size() > 64 ||
            !document.at("ray_query").is_boolean() || !document.at("entries").is_array() ||
            document.at("entries").empty() || document.at("entries").size() > 256)
        {
            throw std::invalid_argument("Unsupported cooked shader program");
        }
        program.rayQuery = document.at("ray_query").get<bool>();
        std::set<std::string> names;
        for (const auto& entry : document.at("entries"))
        {
            const auto  name  = entry.at("name").get<std::string>();
            const auto& stage = entry.at("stage");
            const auto& code  = entry.at("code").get_binary();
            if (name.empty() || name.size() > 4096 || name.find(char(0)) != std::string::npos ||
                !names.insert(name).second || !stage.is_number_unsigned() || stage.get<uint64_t>() > UINT32_MAX ||
                !validStage(stage.get<uint32_t>()) || code.size() < 20 || code.size() % sizeof(uint32_t) != 0)
            {
                throw std::invalid_argument("Invalid cooked shader entry: " + name);
            }
            CompiledShader shader {{name, VriShaderStageBits(stage.get<uint32_t>())},
                                   std::vector<uint32_t>(code.size() / sizeof(uint32_t))};
            std::memcpy(shader.words.data(), code.data(), code.size());
            shader.colorOutputs = entry.at("color_outputs").get<uint32_t>();
            shader.threadGroup  = entry.at("thread_group").get<std::array<uint32_t, 3>>();
            if (shader.colorOutputs > 8)
            {
                throw std::invalid_argument("Invalid cooked fragment output count");
            }
            if (shader.words.front() != 0x07230203)
            {
                throw std::invalid_argument("Invalid cooked SPIR-V: " + name);
            }
            program.shaders.push_back(std::move(shader));
        }
        uint32_t nodes     = 0;
        program.parameters = decodeParameter(document.at("parameters"), 0, nodes);
        if (!document.at("dependencies").is_array() || document.at("dependencies").size() > 10000)
        {
            throw std::invalid_argument("Invalid cooked shader dependencies");
        }
        for (const auto& dependency : document.at("dependencies"))
        {
            const auto path = dependency.at("path").get<std::string>();
            if (path.empty() || path.size() > 4096)
            {
                throw std::invalid_argument("Invalid shader dependency path");
            }
            program.dependencies.push_back({std::filesystem::path(path), dependency.at("hash").get<uint64_t>()});
        }
        return program;
    }

    void writeShaderArchive(const std::filesystem::path& file, const Json& document)
    {
        static_assert(std::endian::native == std::endian::little);
        if (file.extension() != ".vshaderc")
        {
            throw std::invalid_argument("Cooked shader output must use .vshaderc");
        }
        const auto payload = Json::to_cbor(document);
        if (payload.size() > kMaxFileSize - kHeaderSize)
        {
            throw std::invalid_argument("Cooked shader exceeds 64 MiB");
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
            std::filesystem::create_directories(file.parent_path());
        }
        writeFileAtomically(file, std::as_bytes(std::span(bytes)));
    }

    Json readShaderArchive(const std::filesystem::path& file)
    {
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() < std::streamoff(kHeaderSize) || input.tellg() > std::streamoff(kMaxFileSize))
        {
            throw std::runtime_error("Missing or invalid cooked shader: " + file.string());
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!input)
        {
            throw std::runtime_error("Read cooked shader: " + file.string());
        }
        return readShaderArchive(std::as_bytes(std::span(bytes)), file);
    }

    Json readShaderArchive(std::span<const std::byte> data, const std::filesystem::path& file)
    {
        if (data.size() < kHeaderSize || data.size() > kMaxFileSize)
        {
            throw std::runtime_error("Invalid cooked shader size: " + file.string());
        }
        const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(data.data()), data.size());
        if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
        {
            throw std::runtime_error("Invalid cooked shader header: " + file.string());
        }
        uint64_t expected = 0;
        for (size_t index = 0; index < sizeof(expected); ++index)
        {
            expected |= uint64_t(bytes[8 + index]) << (8 * index);
        }
        if (XXH3_64bits(bytes.data() + kHeaderSize, bytes.size() - kHeaderSize) != expected)
        {
            throw std::runtime_error("Cooked shader checksum failed: " + file.string());
        }
        auto document = Json::from_cbor(bytes.begin() + kHeaderSize, bytes.end());
        if (!document.at("version").is_number_integer() || document.at("version") != 1 ||
            (document.at("kind") != "raw_slang" && document.at("kind") != "game_shader"))
        {
            throw std::invalid_argument("Unsupported cooked shader format: " + file.string());
        }
        return document;
    }
} // namespace vultra::detail
