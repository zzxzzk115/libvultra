#include "vultra/function/rendering/srp/builtin/render_graph_backbuffer.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/rendering/srp/builtin/render_graph_resource_names.hpp"
#include "vultra/function/rendering/srp/render_view.hpp"

#include <fg/FrameGraph.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <optional>

namespace vultra
{
    namespace
    {
        [[nodiscard]] std::optional<std::string> selectorString(const nlohmann::json& selector, const char* key)
        {
            if (!selector.is_object())
                return std::nullopt;
            const auto it = selector.find(key);
            if (it != selector.end() && it->is_string())
                return it->get<std::string>();
            const auto descIt = selector.find("desc");
            if (descIt == selector.end() || !descIt->is_object())
                return std::nullopt;
            const auto nestedIt = descIt->find(key);
            if (nestedIt == descIt->end() || !nestedIt->is_string())
                return std::nullopt;
            return nestedIt->get<std::string>();
        }

        void warnMissingBackbufferOnce(std::string_view resourceName, const RenderGraphBackbufferView view)
        {
            static std::array<bool, 3> warned {};
            const auto                 index = static_cast<size_t>(view);
            if (index < warned.size() && warned[index])
                return;
            if (index < warned.size())
                warned[index] = true;

            const char* label = "current";
            if (view == RenderGraphBackbufferView::eLeft)
                label = "left";
            else if (view == RenderGraphBackbufferView::eRight)
                label = "right";

            VULTRA_CORE_ERROR("[DeclarativeRenderer] Render graph requested {} backbuffer '{}' but no target is "
                              "available for the current view.",
                              label,
                              resourceName);
        }
    } // namespace

    std::string normalizeRenderGraphId(std::string value)
    {
        for (auto& ch : value)
        {
            if (ch == '-' || ch == ' ')
                ch = '_';
            else
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    }

    RenderGraphBackbufferView backbufferViewFromResource(std::string_view name)
    {
        const auto normalized = normalizeRenderGraphId(std::string(name));
        if (normalized == render_graph_names::kLeftBackbuffer)
            return RenderGraphBackbufferView::eLeft;
        if (normalized == render_graph_names::kRightBackbuffer)
            return RenderGraphBackbufferView::eRight;
        return RenderGraphBackbufferView::eCurrent;
    }

    RenderGraphBackbufferView backbufferViewFromSelector(const nlohmann::json&           selector,
                                                         const RenderGraphBackbufferView fallback)
    {
        const auto view = selectorString(selector, render_graph_names::kSelectorViewKey.data());
        if (!view)
            return fallback;

        const auto normalized = normalizeRenderGraphId(*view);
        if (normalized == render_graph_names::kViewLeft)
            return RenderGraphBackbufferView::eLeft;
        if (normalized == render_graph_names::kViewRight)
            return RenderGraphBackbufferView::eRight;
        return fallback;
    }

    bool isBackbufferResource(std::string_view name)
    {
        const auto normalized = normalizeRenderGraphId(std::string(name));
        return normalized == render_graph_names::kBackbuffer || normalized == render_graph_names::kTarget ||
               backbufferViewFromResource(name) != RenderGraphBackbufferView::eCurrent;
    }

    FrameGraphResource importRenderGraphBackbuffer(FrameGraph&            fg,
                                                   const RenderView&      view,
                                                   std::string_view       resourceName,
                                                   const nlohmann::json&  selector,
                                                   const std::string_view importName)
    {
        const auto requestedView = backbufferViewFromSelector(selector, backbufferViewFromResource(resourceName));

        rhi::Texture* target   = view.target;
        uint32_t      viewMask = view.renderTargetViewMask();
        if (requestedView == RenderGraphBackbufferView::eLeft || requestedView == RenderGraphBackbufferView::eRight)
        {
            const auto eyeIndex = requestedView == RenderGraphBackbufferView::eLeft ? 0u : 1u;
            if (view.xrEyeTargets[eyeIndex])
            {
                target   = view.xrEyeTargets[eyeIndex];
                viewMask = 0u;
            }
            else
            {
                // No XR eye target available (e.g. the XR session closed and this view fell
                // back to mono). Degrade gracefully instead of failing the pass with an invalid
                // resource (which crashes the FrameGraph): map the LEFT/source eye to the current
                // view target so the graph still composes to the screen, and skip the RIGHT/synth
                // eye (returning {}) so we don't import the same backbuffer texture twice.
                warnMissingBackbufferOnce(resourceName, requestedView);
                if (requestedView == RenderGraphBackbufferView::eRight)
                    return {};
                // eLeft: fall through with target/viewMask at their view.target defaults.
            }
        }

        if (!target)
            return {};

        std::string name {importName};
        if (requestedView == RenderGraphBackbufferView::eLeft)
            name += "/Left";
        else if (requestedView == RenderGraphBackbufferView::eRight)
            name += "/Right";
        return framegraph::importTexture(fg, name, target, viewMask);
    }
} // namespace vultra
