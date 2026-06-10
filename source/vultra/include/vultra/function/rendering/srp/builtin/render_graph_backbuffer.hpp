#pragma once

#include <fg/FrameGraphResource.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <string_view>

class FrameGraph;

namespace vultra
{
    struct RenderView;

    // Backbuffer-view resolution shared by the declarative renderer runtime and the
    // builtin render-graph passes (FinalComposition). A render-graph resource or a
    // pass `view` selector may address the current view target or an explicit XR eye.
    enum class RenderGraphBackbufferView : uint8_t
    {
        eCurrent,
        eLeft,
        eRight,
    };

    // Canonical id form used when comparing render-graph names: lowercase, '-'/' ' -> '_'.
    [[nodiscard]] std::string normalizeRenderGraphId(std::string value);

    [[nodiscard]] RenderGraphBackbufferView backbufferViewFromResource(std::string_view name);

    [[nodiscard]] RenderGraphBackbufferView backbufferViewFromSelector(const nlohmann::json&           selector,
                                                                       const RenderGraphBackbufferView fallback);

    [[nodiscard]] bool isBackbufferResource(std::string_view name);

    // Imports the requested backbuffer (current view target or an explicit XR eye) into
    // the frame graph. Degrades gracefully when the requested eye target is unavailable:
    // LEFT falls back to the current view target, RIGHT returns a null resource.
    [[nodiscard]] FrameGraphResource importRenderGraphBackbuffer(FrameGraph&            fg,
                                                                 const RenderView&      view,
                                                                 std::string_view       resourceName,
                                                                 const nlohmann::json&  selector,
                                                                 const std::string_view importName);
} // namespace vultra
