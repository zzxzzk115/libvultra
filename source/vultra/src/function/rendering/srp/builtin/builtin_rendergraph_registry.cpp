#include "vultra/function/rendering/srp/builtin/builtin_rendergraph_registry.hpp"

#include <builtin_rendergraphs.hpp>

#include <array>

namespace vultra
{
    namespace
    {
        constexpr std::array<BuiltinRenderGraphSource, builtin_rendergraph_sources_count> kSources {
#define VULTRA_BUILTIN_RENDERGRAPH_RECORD(uri, renderer_key, symbol) \
    BuiltinRenderGraphSource {uri, renderer_key, symbol, symbol##_size},
            VULTRA_BUILTIN_RENDERGRAPH_SOURCES(VULTRA_BUILTIN_RENDERGRAPH_RECORD)
#undef VULTRA_BUILTIN_RENDERGRAPH_RECORD
        };
    } // namespace

    std::span<const BuiltinRenderGraphSource> builtinRenderGraphSources()
    {
        return std::span<const BuiltinRenderGraphSource> {kSources.data(), kSources.size()};
    }

    const BuiltinRenderGraphSource* findBuiltinRenderGraphSource(std::string_view uri)
    {
        for (const auto& source : kSources)
        {
            if (source.uri == uri)
                return &source;
        }
        return nullptr;
    }

    std::string_view builtinRenderGraphText(std::string_view uri)
    {
        const auto* source = findBuiltinRenderGraphSource(uri);
        if (!source)
            return {};
        return std::string_view {reinterpret_cast<const char*>(source->data), source->size};
    }
} // namespace vultra
