#pragma once

#include "vultra/core/base/uuid.hpp"

#include <vasset/vtexture.hpp>
#include <vbase/core/result.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vultra::asset_io
{
    // Builtin-asset URI classification and loading, extracted from asset_system.cpp (god-file split, see
    // doc/architecture/asset-system.md). The platform-specific builtin texture resource loading (Windows
    // .rc resources / linker-embedded symbols) is isolated inside the .cpp so the `#ifdef` surface stays
    // in one translation unit.

    [[nodiscard]] bool isBuiltinTextureUri(std::string_view uri);
    [[nodiscard]] bool isBuiltinMaterialUri(std::string_view uri);

    [[nodiscard]] std::filesystem::path builtinTexturePathForUri(std::string_view uri);
    [[nodiscard]] std::filesystem::path builtinMaterialPathForUri(std::string_view uri);

    [[nodiscard]] vbase::Result<std::string, std::string> readBuiltinTextFile(const std::filesystem::path& path);

    // Reverse lookup: the builtin texture URI for a UUID (scans builtin/textures); empty if none.
    [[nodiscard]] std::string builtinTextureUriForUuid(const CoreUUID& uuid);

    // Embedded builtin texture bytes (Windows .rc resource / linker-embedded symbols).
    [[nodiscard]] vbase::Result<std::vector<std::byte>, std::string> readResourceBuiltinTextureBytes(std::string_view uri);

    // Decode builtin texture bytes into a CPU VTexture, resolving the file format from the URI's path.
    [[nodiscard]] std::unique_ptr<vasset::VTexture> makeTextureFromBytes(std::string_view              uri,
                                                                         const std::vector<std::byte>& bytes);
} // namespace vultra::asset_io
