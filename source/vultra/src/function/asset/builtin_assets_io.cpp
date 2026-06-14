#include "vultra/function/asset/builtin_assets_io.hpp"

#include "vultra/core/builtin/builtin_resources.hpp"       // builtin::list() pack enumeration
#include "vultra/function/asset/builtin_assets.hpp"       // kBuiltin* URI constants + UUID helpers
#include "vultra/function/asset/builtin_resource_ids.hpp" // kBuiltinResourceCitrusOrchardSkyTexture

#include <algorithm>
#include <cstring>
#include <fstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
extern "C"
{
    extern const std::byte vultra_builtin_citrus_orchard_sky_texture_start[];
    extern const std::byte vultra_builtin_citrus_orchard_sky_texture_end[];
}
#endif

namespace vultra::asset_io
{
    namespace
    {
        bool isBuiltinCitrusOrchardSkyTextureUri(std::string_view uri)
        {
            return uri == kBuiltinCitrusOrchardSkyTextureUri;
        }

        bool isLoadableBuiltinTexturePath(const std::filesystem::path& path)
        {
            const auto ext = path.extension().generic_string();
            return ext == ".vtexture" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
                   ext == ".tga" || ext == ".hdr" || ext == ".pic" || ext == ".exr" || ext == ".ktx" ||
                   ext == ".ktx2" || ext == ".dds";
        }

        std::string builtinTextureUriForPath(const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto rel = std::filesystem::relative(path.lexically_normal(),
                                                       (std::filesystem::path("builtin") / "textures").lexically_normal(),
                                                       ec);
            if (ec || rel.empty())
                return {};
            return std::string(kBuiltinTextureUriPrefix) + rel.generic_string();
        }

        vasset::VTextureFileFormat textureFileFormatForExtension(const std::filesystem::path& path)
        {
            const auto ext = path.extension().generic_string();
            using enum vasset::VTextureFileFormat;
            if (ext == ".jpg")
                return eJPG;
            if (ext == ".jpeg")
                return eJPEG;
            if (ext == ".png")
                return ePNG;
            if (ext == ".tga")
                return eTGA;
            if (ext == ".bmp")
                return eBMP;
            if (ext == ".hdr")
                return eHDR;
            if (ext == ".pic")
                return ePIC;
            if (ext == ".exr")
                return eEXR;
            if (ext == ".ktx")
                return eKTX;
            if (ext == ".dds")
                return eDDS;
            if (ext == ".ktx2")
                return eKTX2;
            return eUnknown;
        }

        std::unique_ptr<vasset::VTexture> makeTextureFromBytes(std::string_view               uri,
                                                               const std::filesystem::path&   path,
                                                               const std::vector<std::byte>& bytes)
        {
            if (path.extension() == ".vtexture")
            {
                auto cpu = std::make_unique<vasset::VTexture>();
                auto r   = vasset::loadTextureFromMemory(bytes, *cpu);
                return r ? std::move(cpu) : nullptr;
            }

            auto fileFormat = textureFileFormatForExtension(path);
            if (fileFormat == vasset::VTextureFileFormat::eUnknown)
                return nullptr;

            auto cpu        = std::make_unique<vasset::VTexture>();
            cpu->uuid       = builtinTextureUuidForUri(uri).native();
            cpu->fileFormat = fileFormat;
            cpu->data.resize(bytes.size());
            std::memcpy(cpu->data.data(), bytes.data(), bytes.size());
            return cpu;
        }
    } // namespace

    bool isBuiltinTextureUri(std::string_view uri)
    {
        return uri.starts_with(kBuiltinTextureUriPrefix);
    }

    bool isBuiltinMaterialUri(std::string_view uri)
    {
        return uri.starts_with(kBuiltinMaterialUriPrefix);
    }

    bool isBuiltinFontUri(std::string_view uri) { return uri.starts_with(kBuiltinFontUriPrefix); }

    std::filesystem::path builtinTexturePathForUri(std::string_view uri)
    {
        if (!isBuiltinTextureUri(uri))
            return {};
        const auto rel = std::string(uri.substr(kBuiltinTextureUriPrefix.size()));
        return (std::filesystem::path("builtin") / "textures" / std::filesystem::path(rel)).lexically_normal();
    }

    std::filesystem::path builtinMaterialPathForUri(std::string_view uri)
    {
        if (!isBuiltinMaterialUri(uri))
            return {};
        const auto rel = std::string(uri.substr(kBuiltinMaterialUriPrefix.size()));
        return (std::filesystem::path("builtin") / "materials" / std::filesystem::path(rel)).lexically_normal();
    }

    std::filesystem::path builtinFontPathForUri(std::string_view uri)
    {
        if (!isBuiltinFontUri(uri))
            return {};
        const auto rel = std::string(uri.substr(kBuiltinFontUriPrefix.size()));
        return (std::filesystem::path("builtin") / "fonts" / std::filesystem::path(rel)).lexically_normal();
    }

    vbase::Result<std::string, std::string> readBuiltinTextFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return vbase::Result<std::string, std::string>::err("builtin text asset not found");

        const auto  size = static_cast<std::streamsize>(file.tellg());
        std::string text(static_cast<size_t>(std::max<std::streamsize>(size, 0)), '\0');
        file.seekg(0);
        if (!text.empty() && !file.read(text.data(), size))
            return vbase::Result<std::string, std::string>::err("failed to read builtin text asset");
        return vbase::Result<std::string, std::string>::ok(std::move(text));
    }

    std::string builtinTextureUriForUuid(const CoreUUID& uuid)
    {
        if (uuid == builtinCitrusOrchardSkyTextureUuid())
            return std::string(kBuiltinCitrusOrchardSkyTextureUri);

        // Packaged runtime has no builtin/ folder on disk: enumerate the mounted builtin pack
        // (logical paths like "textures/icons/folder.vtexture") and match by UUID. The editor also
        // has the pack mounted, so this path covers both.
        for (const auto& logical : vultra::builtin::list("textures/"))
        {
            const auto uri = std::string("builtin://") + logical;
            if (builtinTextureUuidForUri(uri) == uuid)
                return uri;
        }

        const auto root = std::filesystem::path("builtin") / "textures";
        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || ec)
            return {};
        for (auto it = std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator {};
             it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }
            const auto& entry = *it;
            if (!entry.is_regular_file(ec) || ec || !isLoadableBuiltinTexturePath(entry.path()))
            {
                ec.clear();
                continue;
            }
            const auto uri = builtinTextureUriForPath(entry.path());
            if (!uri.empty() && builtinTextureUuidForUri(uri) == uuid)
                return uri;
        }
        return {};
    }

    std::string builtinFontUriForUuid(const CoreUUID& uuid)
    {
        // Mounted builtin pack (works in the packaged runtime where builtin/fonts is not on disk,
        // and also covers the editor since the pack is mounted there too).
        for (const auto& logical : vultra::builtin::list("fonts/"))
        {
            const auto uri = std::string("builtin://") + logical;
            if (builtinFontUuidForUri(uri) == uuid)
                return uri;
        }

        // Project-added builtin fonts on disk (editor).
        const auto      root = std::filesystem::path("builtin") / "fonts";
        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || ec)
            return {};
        for (auto it = std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator {};
             it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }
            const auto& entry = *it;
            if (!entry.is_regular_file(ec) || ec)
            {
                ec.clear();
                continue;
            }
            const auto ext = entry.path().extension().generic_string();
            if (ext != ".ttf" && ext != ".otf")
                continue;
            const auto rel = std::filesystem::relative(entry.path().lexically_normal(), root.lexically_normal(), ec);
            if (ec || rel.empty())
            {
                ec.clear();
                continue;
            }
            const auto uri = std::string(kBuiltinFontUriPrefix) + rel.generic_string();
            if (builtinFontUuidForUri(uri) == uuid)
                return uri;
        }
        return {};
    }

    vbase::Result<std::vector<std::byte>, std::string> readResourceBuiltinTextureBytes(std::string_view uri)
    {
        if (!isBuiltinCitrusOrchardSkyTextureUri(uri))
            return vbase::Result<std::vector<std::byte>, std::string>::err("builtin texture resource not found");

#if defined(_WIN32)
        HMODULE module = GetModuleHandleW(nullptr);
        HRSRC   res    = FindResourceW(module,
                                       MAKEINTRESOURCEW(kBuiltinResourceCitrusOrchardSkyTexture),
                                       MAKEINTRESOURCEW(10));
        if (!res)
            return vbase::Result<std::vector<std::byte>, std::string>::err("builtin texture resource not found");

        HGLOBAL     loaded = LoadResource(module, res);
        const auto* data   = loaded ? static_cast<const std::byte*>(LockResource(loaded)) : nullptr;
        const DWORD size   = SizeofResource(module, res);
        if (!data || size == 0)
            return vbase::Result<std::vector<std::byte>, std::string>::err("builtin texture resource is empty");

        std::vector<std::byte> bytes(size);
        std::memcpy(bytes.data(), data, size);
        return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(bytes));
#elif !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
        const auto* begin = vultra_builtin_citrus_orchard_sky_texture_start;
        const auto* end   = vultra_builtin_citrus_orchard_sky_texture_end;
        if (!begin || !end || end <= begin)
            return vbase::Result<std::vector<std::byte>, std::string>::err("builtin texture resource is empty");

        std::vector<std::byte> bytes(static_cast<size_t>(end - begin));
        std::memcpy(bytes.data(), begin, bytes.size());
        return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(bytes));
#else
        return vbase::Result<std::vector<std::byte>, std::string>::err("builtin texture resource not available");
#endif
    }

    std::unique_ptr<vasset::VTexture> makeTextureFromBytes(std::string_view uri, const std::vector<std::byte>& bytes)
    {
        return makeTextureFromBytes(uri, builtinTexturePathForUri(uri), bytes);
    }
} // namespace vultra::asset_io
