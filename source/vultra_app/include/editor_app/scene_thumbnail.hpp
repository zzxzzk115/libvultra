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
} // namespace vultra_app
