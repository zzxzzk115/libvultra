#pragma once

#include "vultra/function/scene/vscn_document.hpp"

#include <string_view>

namespace vultra
{
    class VSceneReader
    {
    public:
        // Parse .vscn text into document.
        // Throws std::runtime_error on malformed input.
        static VSceneDocument parse(std::string_view text);
    };
} // namespace vultra
