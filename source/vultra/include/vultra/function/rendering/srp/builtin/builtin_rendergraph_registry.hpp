#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace vultra
{
    struct BuiltinRenderGraphSource
    {
        const char*         uri;
        const char*         rendererKey;
        const unsigned char* data;
        std::size_t         size;
    };

    std::span<const BuiltinRenderGraphSource> builtinRenderGraphSources();
    const BuiltinRenderGraphSource*           findBuiltinRenderGraphSource(std::string_view uri);
    std::string_view                          builtinRenderGraphText(std::string_view uri);
} // namespace vultra
