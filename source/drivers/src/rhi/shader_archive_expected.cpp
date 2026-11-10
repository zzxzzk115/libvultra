#include "shader_archive.hpp"

#include <xxhash.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <set>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr size_t kHeaderSize = 16;
        constexpr size_t kMaxFileSize = 64 * 1024 * 1024;
        constexpr std::array<uint8_t, 8> kMagic {'V', 'U', 'L', 'T', 'R', 'A', 'S', 'H'};

        const Json* member(const Json& object, const char* name)
        {
            if (!object.is_object()) return nullptr;
            const auto it = object.find(name);
            return it == object.end() ? nullptr : &*it;
        }

        bool text(const Json& object, const char* key, std::string& out)
        {
            const auto* value = member(object, key);
            if (!value || !value->is_string()) return false;
            out = value->get_ref<const std::string&>();
            return true;
        }

        bool number(const Json& object, const char* key, uint64_t& out)
        {
            const auto* value = member(object, key);
            if (!value || !value->is_number_unsigned()) return false;
            out = value->get<uint64_t>();
            return true;
        }

        bool boolean(const Json& object, const char* key, bool& out)
        {
            const auto* value = member(object, key);
            if (!value || !value->is_boolean()) return false;
            out = value->get<bool>();
            return true;
        }

        bool u32(const Json& object, const char* key, uint32_t& out)
        {
            uint64_t value;
            if (!number(object, key, value) || value > UINT32_MAX) return false;
            out = uint32_t(value);
            return true;
        }

        std::expected<ShaderParameter, std::string> parameter(const Json& doc, uint32_t depth, uint32_t& nodes)
        {
            if (depth > 64 || ++nodes > 100000)
                return std::unexpected("Excessively large shader parameter layout");
            ShaderParameter result;
            uint32_t kind, scalar, view;
            if (!text(doc, "name", result.name) || !text(doc, "type", result.typeName) || result.name.size() > 4096 ||
                result.typeName.size() > 4096 || !u32(doc, "kind", kind) ||
                kind > uint32_t(ShaderParameterKind::eSampler) || !u32(doc, "scalar", scalar) ||
                scalar > uint32_t(ShaderScalarType::eFloat16) || !number(doc, "size", result.size) ||
                !number(doc, "stride", result.stride) || !number(doc, "count", result.count) ||
                !number(doc, "matrix_stride", result.matrixStride) || !u32(doc, "rows", result.rows) ||
                !u32(doc, "columns", result.columns) || !boolean(doc, "column_major", result.columnMajor) ||
                !u32(doc, "view_type", view) || view > uint32_t(VriTextureViewType_CubeArray))
            {
                return std::unexpected("Invalid cooked shader parameter");
            }
            result.kind = ShaderParameterKind(kind);
            result.scalar = ShaderScalarType(scalar);
            result.viewType = VriTextureViewType(view);
            const auto* descriptor = member(doc, "descriptor_type");
            if (!descriptor) return std::unexpected("Missing shader descriptor type");
            if (!descriptor->is_null())
            {
                uint32_t value;
                if (!u32(doc, "descriptor_type", value) || value > uint32_t(VriDescriptorType_AccelerationStructure))
                    return std::unexpected("Invalid cooked shader descriptor type");
                result.descriptorType = VriDescriptorType(value);
            }
            const auto* offsets = member(doc, "offsets");
            const auto* fields = member(doc, "fields");
            if (!offsets || !offsets->is_array() || !fields || !fields->is_array())
                return std::unexpected("Invalid cooked shader layout children");
            std::set<uint32_t> kinds;
            for (const auto& offset : *offsets)
            {
                uint32_t offsetKind;
                uint64_t value, space;
                if (!u32(offset, "kind", offsetKind) || offsetKind > uint32_t(ShaderOffsetKind::ePushConstant) ||
                    !kinds.insert(offsetKind).second || !number(offset, "offset", value) || !number(offset, "space", space))
                    return std::unexpected("Invalid cooked shader layout offset");
                result.offsets.push_back({ShaderOffsetKind(offsetKind), value, space});
            }
            for (const auto& field : *fields)
            {
                auto child = parameter(field, depth + 1, nodes);
                if (!child) return std::unexpected(child.error());
                result.fields.push_back(std::move(*child));
            }
            return result;
        }

        std::expected<ShaderProgram, std::string> decode(const Json& doc)
        {
            ShaderProgram result;
            if (!text(doc, "target", result.profile) || !text(doc, "compile_key", result.compileKey) ||
                result.compileKey.size() > 128 || !boolean(doc, "ray_query", result.rayQuery))
                return std::unexpected("Invalid cooked shader compiler metadata");
            const auto* caps = member(doc, "capabilities");
            const auto* entries = member(doc, "entries");
            const auto* params = member(doc, "parameters");
            const auto* deps = member(doc, "dependencies");
            if (!caps || !caps->is_array() || caps->size() > 128 || !entries || !entries->is_array() ||
                entries->empty() || entries->size() > 256 || !params || !deps || !deps->is_array() || deps->size() > 10000)
                return std::unexpected("Invalid cooked shader layout or metadata");
            for (const auto& cap : *caps)
            {
                if (!cap.is_string()) return std::unexpected("Invalid shader capability");
                result.capabilities.push_back(cap.get_ref<const std::string&>());
            }
            for (const auto& entry : *entries)
            {
                CompiledShader shader;
                uint32_t stage;
                const auto* code = member(entry, "code");
                const auto* group = member(entry, "thread_group");
                if (!text(entry, "name", shader.entry.name) || !u32(entry, "stage", stage) ||
                    !u32(entry, "color_outputs", shader.colorOutputs) || shader.colorOutputs > 8 ||
                    !code || !code->is_binary() || !group || !group->is_array() || group->size() != 3)
                    return std::unexpected("Invalid cooked shader entry");
                shader.entry.stage = VriShaderStageBits(stage);
                for (size_t i = 0; i < 3; ++i)
                {
                    const auto& item = (*group)[i];
                    if (!item.is_number_unsigned() || item.get<uint64_t>() > UINT32_MAX)
                        return std::unexpected("Invalid shader thread group");
                    shader.threadGroup[i] = item.get<uint32_t>();
                }
                const auto& bytes = code->get_binary();
                if (bytes.empty() || (result.profile != "wgsl" && (bytes.size() < 20 || bytes.size() % 4 != 0)))
                    return std::unexpected("Invalid cooked shader bytecode");
                if (result.profile == "wgsl")
                    shader.wgsl.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                else
                {
                    shader.words.resize(bytes.size() / 4);
                    std::memcpy(shader.words.data(), bytes.data(), bytes.size());
                }
                result.shaders.push_back(std::move(shader));
            }
            uint32_t nodes = 0;
            auto layout = parameter(*params, 0, nodes);
            if (!layout) return std::unexpected(layout.error());
            result.parameters = std::move(*layout);
            for (const auto& dep : *deps)
            {
                std::string path;
                uint64_t hash;
                if (!text(dep, "path", path) || path.empty() || path.size() > 4096 || !number(dep, "hash", hash))
                    return std::unexpected("Invalid shader dependency");
                result.dependencies.push_back({std::filesystem::path(path), hash});
            }
            if (auto valid = detail::validateShaderProgramExpected(result); !valid)
                return std::unexpected(valid.error());
            return result;
        }
    }

    namespace detail
    {
        std::expected<ShaderParameter, std::string> decodeShaderParameterExpected(const Json& document)
        {
            uint32_t nodes = 0;
            return parameter(document, 0, nodes);
        }

        std::expected<ShaderProgram, std::string> decodeShaderProgramExpected(const Json& document)
        {
            return decode(document);
        }

        std::expected<Json, std::string> readShaderArchiveExpected(std::span<const std::byte>   data,
                                                                   const std::filesystem::path& identity)
        {
            const auto error = [&](std::string_view reason)
            {
                return std::unexpected(std::string(reason) + ": " + identity.string());
            };
            if (data.size() < kHeaderSize || data.size() > kMaxFileSize)
            {
                return error("Invalid cooked shader size");
            }
            const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
            if (!std::equal(kMagic.begin(), kMagic.end(), bytes))
            {
                return error("Invalid cooked shader header");
            }
            uint64_t checksum = 0;
            for (size_t i = 0; i < sizeof(checksum); ++i)
            {
                checksum |= uint64_t(bytes[8 + i]) << (8 * i);
            }
            if (XXH3_64bits(bytes + kHeaderSize, data.size() - kHeaderSize) != checksum)
            {
                return error("Cooked shader checksum failed");
            }
            auto        document = Json::from_cbor(bytes + kHeaderSize, bytes + data.size(), true, false);
            const auto* version  = member(document, "version");
            std::string kind;
            if (document.is_discarded() || !version || !version->is_number_integer() || *version != 1 ||
                !text(document, "kind", kind) ||
                (kind != "raw_slang" && kind != "game_shader" &&
                 !(kind == "development_shader" && identity.extension() == ".vshadercache")))
            {
                return error("Unsupported cooked shader format");
            }
            return document;
        }

        std::expected<Json, std::string> readShaderArchiveExpected(const std::filesystem::path& file)
        {
            std::ifstream input(file, std::ios::binary | std::ios::ate);
            const auto    length = input ? input.tellg() : std::streampos(-1);
            if (length < std::streamoff(kHeaderSize) || length > std::streamoff(kMaxFileSize))
            {
                return std::unexpected("Missing or invalid cooked shader: " + file.string());
            }
            std::vector<std::byte> bytes(static_cast<size_t>(length));
            input.seekg(0);
            input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!input)
            {
                return std::unexpected("Read cooked shader: " + file.string());
            }
            return readShaderArchiveExpected(bytes, file);
        }
    } // namespace detail

    std::expected<ShaderProgram, std::string> ShaderProgram::loadExpected(std::span<const std::byte>   data,
                                                                          const std::filesystem::path& identity)
    {
        auto document = detail::readShaderArchiveExpected(data, identity);
        if (!document)
        {
            return std::unexpected(document.error());
        }
        if ((*document)["kind"] != "raw_slang")
        {
            return std::unexpected("Expected raw Slang program: " + identity.string());
        }
        auto program = detail::decodeShaderProgramExpected(*document);
        if (!program)
        {
            return std::unexpected(program.error() + ": " + identity.string());
        }
        return program;
    }

    std::expected<ShaderProgram, std::string> ShaderProgram::loadExpected(const std::filesystem::path& file)
    {
        auto document = detail::readShaderArchiveExpected(file);
        if (!document)
        {
            return std::unexpected(document.error());
        }
        if ((*document)["kind"] != "raw_slang")
        {
            return std::unexpected("Expected raw Slang program: " + file.string());
        }
        return detail::decodeShaderProgramExpected(*document);
    }
}
