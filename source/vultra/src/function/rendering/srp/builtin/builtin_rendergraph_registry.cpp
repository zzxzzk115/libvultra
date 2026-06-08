#include "vultra/function/rendering/srp/builtin/builtin_rendergraph_registry.hpp"

#include "vultra/core/builtin/builtin_resources.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace vultra
{
    namespace
    {
        constexpr std::string_view kRenderPrefix = "render/";
        constexpr std::string_view kScheme       = "builtin://";

        std::string rendererKeyFromName(std::string_view name)
        {
            constexpr std::string_view ext = ".vrg.json";
            if (name.size() >= ext.size() && name.substr(name.size() - ext.size()) == ext)
                name.remove_suffix(ext.size());

            std::string key {name};
            for (auto& c : key)
                c = (c == '-' || c == ' ') ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return key;
        }

        // Built once from the mounted builtin pack (which is installed by a static initializer
        // before any render subsystem queries graphs).
        const std::vector<BuiltinRenderGraphSource>& sources()
        {
            static const std::vector<BuiltinRenderGraphSource> s = [] {
                std::vector<BuiltinRenderGraphSource> out;
                for (const auto& logical : builtin::list(kRenderPrefix))
                {
                    const std::string_view name = std::string_view {logical}.substr(kRenderPrefix.size());
                    out.push_back(BuiltinRenderGraphSource {std::string {kScheme} + "render/" + std::string {name},
                                                            rendererKeyFromName(name)});
                }
                std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.uri < b.uri; });
                return out;
            }();
            return s;
        }
    } // namespace

    std::span<const BuiltinRenderGraphSource> builtinRenderGraphSources()
    {
        return std::span<const BuiltinRenderGraphSource> {sources().data(), sources().size()};
    }

    const BuiltinRenderGraphSource* findBuiltinRenderGraphSource(std::string_view uri)
    {
        for (const auto& source : sources())
        {
            if (source.uri == uri)
                return &source;
        }
        return nullptr;
    }

    std::string builtinRenderGraphText(std::string_view uri)
    {
        if (uri.size() < kScheme.size() || uri.substr(0, kScheme.size()) != kScheme)
            return {};

        const std::string      logical {uri.substr(kScheme.size())}; // "render/<file>.vrg.json"
        std::vector<std::byte> raw;
        if (!builtin::read(logical, raw) || raw.empty())
            return {};
        return std::string {reinterpret_cast<const char*>(raw.data()), raw.size()};
    }
} // namespace vultra
