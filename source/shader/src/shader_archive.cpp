#include "shader_archive.hpp"

#include <vultra/platform/os/file.hpp>

#include <xxhash.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <set>

namespace vultra::detail
{
    namespace
    {
        constexpr std::array<uint8_t, 8> kMagic {'V', 'U', 'L', 'T', 'R', 'A', 'S', 'H'};
        constexpr size_t                 kHeaderSize  = 16;
        constexpr size_t                 kMaxFileSize = 64 * 1024 * 1024;
        using Json                                    = nlohmann::json;

        Json encodeAttributes(const std::vector<ShaderAttribute>& attributes)
        {
            Json result = Json::array();
            for (const auto& attribute : attributes)
            {
                Json arguments = Json::array();
                for (const auto& argument : attribute.arguments)
                {
                    arguments.push_back(
                        {{"kind", argument.kind}, {"text", argument.text}, {"number", argument.number}});
                }
                result.push_back({{"name", attribute.name}, {"arguments", arguments}});
            }
            return result;
        }

        std::expected<std::vector<ShaderAttribute>, Error> decodeAttributes(const Json& document)
        {
            if (!document.is_array() || document.size() > 256)
            {
                return makeError("Invalid cooked shader attributes");
            }
            std::vector<ShaderAttribute> result;
            for (const auto& entry : document)
            {
                ShaderAttribute attribute;
                attribute.name = entry.at("name").get<std::string>();
                if (attribute.name.size() > 4096 || !entry.at("arguments").is_array() ||
                    entry.at("arguments").size() > 256)
                {
                    return makeError("Invalid cooked shader attribute");
                }
                for (const auto& value : entry.at("arguments"))
                {
                    const auto kind = value.at("kind").get<uint32_t>();
                    if (kind > uint32_t(ShaderAttributeArgument::Kind::eFloat))
                    {
                        return makeError("Invalid cooked shader attribute argument");
                    }
                    attribute.arguments.push_back({ShaderAttributeArgument::Kind(kind),
                                                   value.at("text").get<std::string>(),
                                                   value.at("number").get<double>()});
                }
                result.push_back(std::move(attribute));
            }
            return result;
        }

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
                    {"descriptor_type", parameter.descriptorType ? Json(*parameter.descriptorType) : Json()},
                    {"view_type", parameter.viewType},
                    {"offsets", offsets},
                    {"fields", fields},
                    {"attributes", encodeAttributes(parameter.attributes)},
                    {"default", parameter.defaultValue ? Json(*parameter.defaultValue) : Json()}};
        }

        std::expected<ShaderParameter, Error> decodeParameter(const Json& document, uint32_t depth, uint32_t& nodes)
        {
            if (depth > 64 || ++nodes > 100000)
            {
                return makeError("Excessively large shader parameter layout");
            }
            ShaderParameter parameter;
            parameter.name     = document.at("name").get<std::string>();
            parameter.typeName = document.at("type").get<std::string>();
            const auto kind    = document.at("kind").get<uint32_t>();
            const auto scalar  = document.at("scalar").get<uint32_t>();
            if (kind > uint32_t(ShaderParameterKind::eSampler) || scalar > uint32_t(ShaderScalarType::eOther) ||
                parameter.name.size() > 4096 || parameter.typeName.size() > 4096)
            {
                return makeError("Invalid cooked shader parameter type");
            }
            parameter.kind         = ShaderParameterKind(kind);
            parameter.scalar       = ShaderScalarType(scalar);
            parameter.size         = document.at("size").get<uint64_t>();
            parameter.stride       = document.at("stride").get<uint64_t>();
            parameter.count        = document.at("count").get<uint64_t>();
            parameter.matrixStride = document.at("matrix_stride").get<uint64_t>();
            parameter.rows         = document.at("rows").get<uint32_t>();
            parameter.columns      = document.at("columns").get<uint32_t>();
            parameter.columnMajor  = document.at("column_major").get<bool>();
            if (!document.at("descriptor_type").is_null())
            {
                const auto descriptor = document.at("descriptor_type").get<uint32_t>();
                if (descriptor > uint32_t(VriDescriptorType_AccelerationStructure))
                {
                    return makeError("Invalid cooked shader descriptor type");
                }
                parameter.descriptorType = VriDescriptorType(descriptor);
            }
            const auto view = document.at("view_type").get<uint32_t>();
            if (view > uint32_t(VriTextureViewType_CubeArray))
            {
                return makeError("Invalid cooked shader texture view type");
            }
            parameter.viewType = VriTextureViewType(view);
            if (!document.at("offsets").is_array() || !document.at("fields").is_array())
            {
                return makeError("Invalid cooked shader layout children");
            }
            std::set<uint32_t> offsetKinds;
            for (const auto& offset : document.at("offsets"))
            {
                const auto offsetKind = offset.at("kind").get<uint32_t>();
                if (offsetKind > uint32_t(ShaderOffsetKind::ePushConstant) || !offsetKinds.insert(offsetKind).second)
                {
                    return makeError("Invalid cooked shader layout offset");
                }
                parameter.offsets.push_back({ShaderOffsetKind(offsetKind),
                                             offset.at("offset").get<uint64_t>(),
                                             offset.at("space").get<uint64_t>()});
            }
            for (const auto& field : document.at("fields"))
            {
                auto child = decodeParameter(field, depth + 1, nodes);
                if (!child)
                {
                    return std::unexpected(child.error());
                }
                parameter.fields.push_back(std::move(*child));
            }
            auto attributes = decodeAttributes(document.at("attributes"));
            if (!attributes)
            {
                return std::unexpected(attributes.error());
            }
            parameter.attributes = std::move(*attributes);
            if (!document.at("default").is_null())
            {
                parameter.defaultValue = document.at("default").get<double>();
            }
            return parameter;
        }

        bool validStage(uint32_t stage)
        {
            return stage == VriShaderStage_Vertex || stage == VriShaderStage_Fragment ||
                   stage == VriShaderStage_Compute || stage == VriShaderStage_Geometry ||
                   stage == VriShaderStage_TessControl || stage == VriShaderStage_TessEval ||
                   stage == VriShaderStage_Task || stage == VriShaderStage_Mesh || stage == VriShaderStage_RayGen ||
                   stage == VriShaderStage_Intersection || stage == VriShaderStage_AnyHit ||
                   stage == VriShaderStage_ClosestHit || stage == VriShaderStage_Miss ||
                   stage == VriShaderStage_Callable;
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
            const auto           bytes = program.profile == "wgsl" ?
                                             std::as_bytes(std::span(shader.wgsl.data(), shader.wgsl.size())) :
                                             std::as_bytes(std::span(shader.words));
            std::vector<uint8_t> code(bytes.size());
            std::memcpy(code.data(), bytes.data(), bytes.size());
            document["entries"].push_back({{"name", shader.entry.name},
                                           {"stage", uint32_t(shader.entry.stage)},
                                           {"color_outputs", shader.colorOutputs},
                                           {"thread_group", shader.threadGroup},
                                           {"attributes", encodeAttributes(shader.attributes)},
                                           {"code", Json::binary(std::move(code))}});
        }
        for (const auto& dependency : program.dependencies)
        {
            document["dependencies"].push_back({{"path", dependency.path.generic_string()}, {"hash", dependency.hash}});
        }
        return document;
    }

    std::expected<ShaderProgram, Error> decodeShaderProgram(const Json& document)
    {
        // nlohmann reports missing keys and mistyped values from at() and get<>() only by throwing.
        try
        {
            ShaderProgram program;
            program.profile      = document.at("target").get<std::string>();
            program.compileKey   = document.at("compile_key").get<std::string>();
            program.capabilities = document.at("capabilities").get<std::vector<std::string>>();
            if (program.compileKey.size() > 128 || program.capabilities.size() > 128)
            {
                return makeError("Invalid cooked shader compiler metadata");
            }
            const bool wgsl = program.profile == "wgsl";
            if ((!wgsl && !program.profile.starts_with("spirv_")) || program.profile.size() > 64 ||
                !document.at("ray_query").is_boolean() || !document.at("entries").is_array() ||
                document.at("entries").empty() || document.at("entries").size() > 256)
            {
                return makeError("Unsupported cooked shader program");
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
                    !validStage(stage.get<uint32_t>()) || code.empty() ||
                    (!wgsl && (code.size() < 20 || code.size() % sizeof(uint32_t) != 0)))
                {
                    return makeError("Invalid cooked shader entry: {}", name);
                }
                CompiledShader shader;
                shader.entry = {name, VriShaderStageBits(stage.get<uint32_t>())};
                if (wgsl)
                {
                    shader.wgsl.assign(reinterpret_cast<const char*>(code.data()), code.size());
                }
                else
                {
                    shader.words.resize(code.size() / sizeof(uint32_t));
                    std::memcpy(shader.words.data(), code.data(), code.size());
                }
                shader.colorOutputs = entry.at("color_outputs").get<uint32_t>();
                shader.threadGroup  = entry.at("thread_group").get<std::array<uint32_t, 3>>();
                auto attributes     = decodeAttributes(entry.at("attributes"));
                if (!attributes)
                {
                    return std::unexpected(attributes.error());
                }
                shader.attributes = std::move(*attributes);
                if (shader.colorOutputs > 8)
                {
                    return makeError("Invalid cooked fragment output count");
                }
                if (!wgsl && shader.words.front() != 0x07230203)
                {
                    return makeError("Invalid cooked SPIR-V: {}", name);
                }
                program.shaders.push_back(std::move(shader));
            }
            uint32_t nodes      = 0;
            auto     parameters = decodeParameter(document.at("parameters"), 0, nodes);
            if (!parameters)
            {
                return std::unexpected(parameters.error());
            }
            program.parameters = std::move(*parameters);
            if (!document.at("dependencies").is_array() || document.at("dependencies").size() > 10000)
            {
                return makeError("Invalid cooked shader dependencies");
            }
            for (const auto& dependency : document.at("dependencies"))
            {
                const auto path = dependency.at("path").get<std::string>();
                if (path.empty() || path.size() > 4096)
                {
                    return makeError("Invalid shader dependency path");
                }
                program.dependencies.push_back({std::filesystem::path(path), dependency.at("hash").get<uint64_t>()});
            }
            return program;
        }
        catch (const Json::exception& error)
        {
            return makeError("Invalid cooked shader program: {}", error.what());
        }
    }

    std::expected<void, Error> writeShaderArchive(const std::filesystem::path& file, const Json& document)
    {
        static_assert(std::endian::native == std::endian::little);
        const auto kind             = document.find("kind");
        const bool developmentCache = kind != document.end() && *kind == "development_shader";
        const auto extension        = developmentCache ? ".vshadercache" : ".vshaderc";
        if (file.extension() != extension)
        {
            return makeError("Shader output must use {}", extension);
        }
        const auto payload = Json::to_cbor(document);
        if (payload.size() > kMaxFileSize - kHeaderSize)
        {
            return makeError("Cooked shader exceeds 64 MiB");
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
                return makeError("Create shader output directory {}: {}", file.parent_path().string(), error.message());
            }
        }
        return writeFileAtomically(file, std::as_bytes(std::span(bytes)));
    }

    std::expected<Json, Error> readShaderArchive(const std::filesystem::path& file)
    {
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() < std::streamoff(kHeaderSize) || input.tellg() > std::streamoff(kMaxFileSize))
        {
            return makeError("Missing or invalid cooked shader: {}", file.string());
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!input)
        {
            return makeError("Read cooked shader: {}", file.string());
        }
        return readShaderArchive(std::as_bytes(std::span(bytes)), file);
    }

    std::expected<Json, Error> readShaderArchive(std::span<const std::byte> data, const std::filesystem::path& file)
    {
        if (data.size() < kHeaderSize || data.size() > kMaxFileSize)
        {
            return makeError("Invalid cooked shader size: {}", file.string());
        }
        const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(data.data()), data.size());
        if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
        {
            return makeError("Invalid cooked shader header: {}", file.string());
        }
        uint64_t expected = 0;
        for (size_t index = 0; index < sizeof(expected); ++index)
        {
            expected |= uint64_t(bytes[8 + index]) << (8 * index);
        }
        if (XXH3_64bits(bytes.data() + kHeaderSize, bytes.size() - kHeaderSize) != expected)
        {
            return makeError("Cooked shader checksum failed: {}", file.string());
        }
        auto document = Json::from_cbor(bytes.begin() + kHeaderSize, bytes.end(), true, false);
        if (document.is_discarded())
        {
            return makeError("Invalid cooked shader payload: {}", file.string());
        }
        const auto version = document.find("version");
        const auto kind    = document.find("kind");
        if (version == document.end() || kind == document.end() || !version->is_number_integer() || *version != 1 ||
            (*kind != "raw_slang" && !(*kind == "development_shader" && file.extension() == ".vshadercache")))
        {
            return makeError("Unsupported cooked shader format: {}", file.string());
        }
        return document;
    }

    std::expected<std::filesystem::path, Error> absolutePath(const std::filesystem::path& path)
    {
        std::error_code error;
        auto            result = std::filesystem::absolute(path, error);
        if (error)
        {
            return makeError("Resolve shader path {}: {}", path.string(), error.message());
        }
        return result;
    }
} // namespace vultra::detail
