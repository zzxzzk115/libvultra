#pragma once

#include "vultra/core/base/uuid.hpp"

#include <string>
#include <string_view>

namespace vultra
{
    inline constexpr std::string_view kBuiltinTextureUriPrefix = "builtin://textures/";
    inline constexpr std::string_view kBuiltinMaterialUriPrefix = "builtin://materials/";
    inline constexpr std::string_view kBuiltinFontUriPrefix = "builtin://fonts/";
    inline constexpr std::string_view kBuiltinDefaultMaterialUri = "builtin://materials/default.vmat.json";

    // Default font used to render UiTextComponent when no font is assigned. Noto Sans CJK
    // covers Latin + CJK, so unassigned text renders something sensible out of the box.
    inline constexpr std::string_view kBuiltinDefaultFontUri = "builtin://fonts/noto_sans_cjk.otf";

    inline constexpr std::string_view kBuiltinCitrusOrchardSkyTextureUri =
        "builtin://textures/environment_maps/citrus_orchard_puresky_1k.vtexture";

    inline CoreUUID builtinCitrusOrchardSkyTextureUuid()
    {
        static const CoreUUID uuid = [] {
            vbase::UUID parsed {};
            vbase::try_parse_uuid("9a5cf6f3664c0e0b07179f44e84101cc", parsed);
            return CoreUUID(parsed);
        }();
        return uuid;
    }

    inline CoreUUID builtinTextureUuidForUri(std::string_view uri)
    {
        if (uri == kBuiltinCitrusOrchardSkyTextureUri)
            return builtinCitrusOrchardSkyTextureUuid();
        return CoreUUIDHelper::getFromName("builtin-texture:" + std::string(uri));
    }

    // Deterministic UUID for a builtin font URI (e.g. "builtin://fonts/noto_sans_cjk.otf").
    // Mirrors the builtin-texture convention so UiTextComponent::font can store builtin and
    // project fonts uniformly as a CoreUUID.
    inline CoreUUID builtinFontUuidForUri(std::string_view uri)
    {
        return CoreUUIDHelper::getFromName("builtin-font:" + std::string(uri));
    }
} // namespace vultra
