#pragma once

#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/shader_asset.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <cstring>
#include <fstream>

namespace vultra::test
{
    inline std::expected<void, std::string> shaderAssetArchiveExpectedProbe(const ShaderAsset&           asset,
                                                                            const std::filesystem::path& root)
    {
        const auto file = root / "material.vshaderc";
        if (auto saved = asset.saveExpected(file); !saved)
        {
            return std::unexpected(saved.error());
        }
        auto source = AssetSource::create(root);
        if (!source)
        {
            return std::unexpected(source.error());
        }
        auto original = (*source)->readExpected("material.vshaderc");
        if (!original)
        {
            return std::unexpected(original.error());
        }
        auto physical  = ShaderAsset::loadExpected(file);
        auto memory    = ShaderAsset::loadExpected(*original, file);
        auto directory = ShaderAsset::loadExpected("material.vshaderc", source->get());
        if (!physical || !memory || !directory)
        {
            return std::unexpected("Expected game shader file/memory/source loading failed");
        }
        if (auto saved = memory->saveExpected(root / "roundtrip.vshaderc"); !saved)
        {
            return std::unexpected(saved.error());
        }
        auto roundtrip = (*source)->readExpected("roundtrip.vshaderc");
        if (!roundtrip || *roundtrip != *original || memory->name != asset.name || directory->name != asset.name)
        {
            return std::unexpected("Expected game shader roundtrip changed artifact bytes");
        }
        auto invalid = asset;
        invalid.subshaders.front().passes.front().programs.clear();
        if (invalid.saveExpected(file))
        {
            return std::unexpected("Expected game shader save accepted a missing variant");
        }
        invalid = asset;
        invalid.subshaders.front().passes.front().programs.begin()->second.shaders.front().words.clear();
        if (invalid.saveExpected(file) || asset.saveExpected(root / "wrong.extension") ||
            ShaderAsset::loadExpected(root / "absent.vshaderc"))
        {
            return std::unexpected("Expected game shader I/O accepted invalid code, extension or missing file");
        }
        auto retained = (*source)->readExpected("material.vshaderc");
        if (!retained || *retained != *original)
        {
            return std::unexpected("Rejected game shader save replaced the accepted artifact");
        }
        const auto* data     = reinterpret_cast<const uint8_t*>(original->data());
        auto        document = nlohmann::json::from_cbor(data + 16, data + original->size(), true, false);
        if (document.is_discarded())
        {
            return std::unexpected("Game shader fixture has invalid CBOR");
        }
        const auto encode = [](const nlohmann::json& value)
        {
            auto                   payload = nlohmann::json::to_cbor(value);
            std::vector<std::byte> bytes(16 + payload.size());
            constexpr char         magic[] = "VULTRASH";
            std::memcpy(bytes.data(), magic, 8);
            const auto hash = XXH3_64bits(payload.data(), payload.size());
            for (size_t index = 0; index < 8; ++index)
            {
                bytes[8 + index] = std::byte(hash >> (8 * index));
            }
            std::memcpy(bytes.data() + 16, payload.data(), payload.size());
            return bytes;
        };
        size_t     rejections = 0;
        const auto reject     = [&](auto edit) -> std::expected<void, std::string>
        {
            auto candidate = document;
            edit(candidate);
            auto result = ShaderAsset::loadExpected(encode(candidate), "malformed.vshaderc");
            if (result || result.error().find("malformed.vshaderc") == std::string::npos)
            {
                return std::unexpected("Malformed game shader metadata was accepted or lost its identity");
            }
            ++rejections;
            return {};
        };
        for (const auto* key : {"name", "properties", "variants", "subshaders", "source", "compile_key"})
        {
            if (auto result = reject(
                    [&](auto& value)
                    {
                        value.erase(key);
                    });
                !result)
            {
                return result;
            }
        }
        for (const auto edit :
             {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25})
        {
            auto result = reject(
                [&](auto& value)
                {
                    auto& subshader = value["subshaders"][0];
                    auto& pass      = subshader["passes"][0];
                    switch (edit)
                    {
                        case 0:
                            value["kind"] = "raw_slang";
                            break;
                        case 1:
                            value["variants"][0]["location"]["line"] = uint64_t(UINT32_MAX) + 1;
                            break;
                        case 2:
                            subshader["features"] = -1;
                            break;
                        case 3:
                            subshader["state"]["commands"] = nlohmann::json::array();
                            break;
                        case 4:
                            subshader["tags"]["Pipeline"] = 5;
                            break;
                        case 5:
                            pass["entries"][0]["stage"] = uint64_t(UINT32_MAX) + 1;
                            break;
                        case 6:
                            pass["programs"] = nlohmann::json::array();
                            break;
                        case 7:
                            pass["generated"] = 1;
                            break;
                        case 8:
                            pass["programs"].begin().value()["parameters"]["offsets"] = false;
                            break;
                        case 9:
                            value["properties"][0]["default"] = {{"type", 0u}, {"value", "bad"}};
                            break;
                        case 10:
                            value["properties"][0]["default"] = {{"type", 3u}, {"value", {1, 2, 3}}};
                            break;
                        case 11:
                            value["properties"][0]["enum"] = {{"Huge", int64_t(INT32_MAX) + 1}};
                            break;
                        case 12:
                            value["variants"][0].erase("type_conformances");
                            break;
                        case 13:
                            value["variants"][0]["type_conformances"] = nlohmann::json::object();
                            break;
                        case 14:
                            value["variants"][0]["type_conformances"] = {false};
                            break;
                        default: {
                            auto&                conformances = value["variants"][0]["type_conformances"];
                            const nlohmann::json conformance {{"type", "A"}, {"interface", "I"}, {"id", 3u}};
                            conformances = nlohmann::json::array({conformance});
                            switch (edit)
                            {
                                case 15:
                                    conformances[0]["type"] = "";
                                    break;
                                case 16:
                                    conformances[0]["id"] = -1;
                                    break;
                                case 17:
                                    conformances[0]["id"] = uint64_t(UINT32_MAX) + 1;
                                    break;
                                case 18:
                                    conformances[0]["id"] = 3.5;
                                    break;
                                case 19:
                                    conformances.push_back({{"type", "B"}, {"interface", "I"}, {"id", 3u}});
                                    break;
                                case 20:
                                    conformances.push_back({{"type", "A"}, {"interface", "I"}, {"id", 11u}});
                                    break;
                                case 21:
                                    conformances[0]["type"] = std::string("A\0B", 3);
                                    break;
                                case 22:
                                    conformances[0]["type"] = 1;
                                    break;
                                case 23:
                                    conformances[0].erase("interface");
                                    break;
                                case 24:
                                    conformances[0]["type"] = std::string(4097, 'A');
                                    break;
                                case 25:
                                    conformances = std::vector<nlohmann::json>(4097, conformance);
                                    break;
                            }
                            break;
                        }
                    }
                });
            if (!result)
            {
                return result;
            }
        }
        for (int edit = 0; edit < 16; ++edit)
        {
            auto result = reject(
                [&](auto& value)
                {
                    auto& subshader = value["subshaders"][0];
                    auto& surface   = subshader["surface"];
                    surface         = {{"model", "Unlit"},
                                       {"entry", "surfaceMain"},
                                       {"state", subshader["state"]},
                                       {"location", subshader["location"]}};
                    switch (edit)
                    {
                        case 0:
                            subshader.erase("surface");
                            break;
                        case 1:
                            surface = false;
                            break;
                        case 2:
                            surface = nlohmann::json::object();
                            break;
                        case 3:
                            surface["model"] = "Unknown";
                            break;
                        case 4:
                            surface["entry"] = "";
                            break;
                        case 5:
                            surface["entry"] = std::string("a\0b", 3);
                            break;
                        case 6:
                            surface["entry"] = "bad entry";
                            break;
                        case 7:
                            surface["entry"] = std::string(4097, 'a');
                            break;
                        case 8:
                            surface.erase("state");
                            break;
                        case 9:
                            surface.erase("location");
                            break;
                        case 10:
                            surface["state"]["commands"] = false;
                            break;
                        case 11:
                            surface["location"]["line"] = 0u;
                            break;
                        case 12:
                            surface["model"] = 1;
                            break;
                        case 13:
                            surface["entry"] = 1;
                            break;
                        case 14:
                            surface["location"]["file"] = std::string("a\0b", 3);
                            break;
                        case 15:
                            surface                             = nullptr;
                            subshader["passes"][0]["generated"] = true;
                            break;
                    }
                });
            if (!result)
            {
                return result;
            }
        }
        auto corrupt = *original;
        corrupt.back() ^= std::byte(1);
        if (ShaderAsset::loadExpected(corrupt, file) ||
            ShaderAsset::loadExpected(std::span(*original).first(15), file) ||
            !ShaderAsset::loadExpected(*original, file))
        {
            return std::unexpected("Expected game shader checksum/truncation rejection or recovery failed");
        }
        std::printf("Expected game shader archive: file/memory/source byte-exact roundtrip, %zu schema rejections, "
                    "failed-save retention and recovery passed\n",
                    rejections);
        return {};
    }
} // namespace vultra::test
