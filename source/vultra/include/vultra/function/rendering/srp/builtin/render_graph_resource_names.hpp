#pragma once

#include <string_view>

namespace vultra::render_graph_names
{
    // Canonical authoring tokens for the backbuffer / XR eye render targets in a render
    // graph (.vrg.json). These are the single source of truth for the data <-> engine
    // boundary: the editor emits them and the renderer recognizes them. Compare against a
    // name after normalizeId() (lowercase, '-'/space -> '_'); there are no synonym spellings.
    //
    // The engine-internal identities these resolve to are fixed (RenderView::target /
    // xrEyeTargets[0/1]); only the authoring string lives here.

    // Current render target (the view's own target: window backbuffer, camera RT, or the
    // XR stereo swapchain). `kTarget` is the default output-slot name; both mean "current".
    inline constexpr std::string_view kBackbuffer      = "backbuffer";
    inline constexpr std::string_view kTarget          = "target";

    // Explicit XR eye targets (RenderView::xrEyeTargets[0/1]).
    inline constexpr std::string_view kLeftBackbuffer  = "left_backbuffer";
    inline constexpr std::string_view kRightBackbuffer = "right_backbuffer";

    // ResourceRef selector: pick an eye on the canonical backbuffer, e.g.
    // { "resource": "backbuffer", "view": "left" }.
    inline constexpr std::string_view kSelectorViewKey = "view";
    inline constexpr std::string_view kViewLeft        = "left";
    inline constexpr std::string_view kViewRight       = "right";
} // namespace vultra::render_graph_names
