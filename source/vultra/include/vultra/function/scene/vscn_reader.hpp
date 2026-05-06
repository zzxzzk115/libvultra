#pragma once

#include "vultra/function/scene/vscn_document.hpp"

#include <filesystem>
#include <string_view>

namespace vultra
{
    class VscnReader
    {
    public:
        static SceneDocument readFromText(std::string_view text, const std::filesystem::path& baseDir = {});
    };
} // namespace vultra
