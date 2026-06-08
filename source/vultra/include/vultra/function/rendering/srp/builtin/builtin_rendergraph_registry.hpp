#pragma once

#include <span>
#include <string>
#include <string_view>

namespace vultra
{
    struct BuiltinRenderGraphSource
    {
        std::string uri;         // builtin://render/<file>.vrg.json
        std::string rendererKey; // stem, lower-cased, '-'/' ' -> '_'
    };

    std::span<const BuiltinRenderGraphSource> builtinRenderGraphSources();
    const BuiltinRenderGraphSource*           findBuiltinRenderGraphSource(std::string_view uri);
    // JSON text of a builtin render graph by its builtin:// uri (read from the builtin pack).
    std::string                               builtinRenderGraphText(std::string_view uri);
} // namespace vultra
