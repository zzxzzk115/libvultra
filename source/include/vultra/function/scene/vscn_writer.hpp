#pragma once

#include "vultra/function/scene/vscn_document.hpp"

#include <string>

namespace vultra
{
    class VSceneWriter
    {
    public:
        static std::string write(const VSceneDocument& doc);
    };
} // namespace vultra
