#pragma once

#include "editor_app/editor_context.hpp"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace vultra_app
{
    inline std::string sceneThumbnailHash(std::string_view sceneUri)
    {
        uint64_t hash = 1469598103934665603ull;
        for (unsigned char ch : sceneUri)
        {
            hash ^= ch;
            hash *= 1099511628211ull;
        }

        std::ostringstream out;
        out << std::hex << std::setw(16) << std::setfill('0') << hash;
        return out.str();
    }

    inline std::filesystem::path sceneThumbnailPath(const EditorContext& ctx, std::string_view sceneUri)
    {
        if (ctx.state.currentProject.empty() || sceneUri.empty())
            return {};
        return ctx.state.currentProject / ".vultra" / "thumbs" /
               (sceneThumbnailHash(std::string("scene:") + std::string(sceneUri)) + ".scene.png");
    }

    // Deterministic (uri-only) path for a prefab thumbnail, so the content browser can locate it
    // without going through the thumbnail service. Mirrors sceneThumbnailPath.
    inline std::filesystem::path prefabThumbnailPath(const EditorContext& ctx, std::string_view prefabUri)
    {
        if (ctx.state.currentProject.empty() || prefabUri.empty())
            return {};
        return ctx.state.currentProject / ".vultra" / "thumbs" /
               (sceneThumbnailHash(std::string("prefab:") + std::string(prefabUri)) + ".prefab.png");
    }
} // namespace vultra_app
