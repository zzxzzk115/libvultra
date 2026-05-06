#pragma once

#include "vultra/function/scene/vscn_document.hpp"

#include <string>

namespace vultra
{
    class VscnWriter
    {
    public:
        static std::string writeToText(const SceneDocument& doc);
    };
} // namespace vultra
