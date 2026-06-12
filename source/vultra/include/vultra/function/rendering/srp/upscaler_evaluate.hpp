#pragma once

#include "vultra/function/services/render_upscaler_service.hpp"

#include <array>

namespace vultra
{
    struct FrameGraphExecContext;

    struct UpscalerEvaluateTextures
    {
        rhi::Texture* color {nullptr};
        rhi::Texture* output {nullptr};
        rhi::Texture* depth {nullptr};
        rhi::Texture* motion {nullptr};
        rhi::Texture* exposure {nullptr};
        // Per-eye single-layer staging outputs for stereo views. Providers (Streamline/NGX)
        // carry no array-layer information, so two evaluations sharing one layered output
        // image discard each other's result; each eye writes a dedicated image instead and
        // the engine copies the stagings into the layered output afterwards.
        std::array<rhi::Texture*, 2> eyeOutputs {nullptr, nullptr};
    };

    // Evaluates the active upscaler provider for the current view. Single-graph stereo
    // views with layered color/output textures are evaluated once per eye: inputs are
    // sliced to per-layer image views, constants come from the matching eye camera, and
    // each eye gets its own viewport id (temporal history) inside the provider. Eye
    // results land in eyeOutputs and are blitted into the layered output's layers.
    // Returns false when evaluation did not happen or failed; the caller is expected to
    // blit-fallback and call FrameGraphExecContext::clear() itself.
    [[nodiscard]] bool evaluateUpscalerForView(FrameGraphExecContext&          rc,
                                               IRenderUpscalerService&         upscaler,
                                               const UpscalerSettings&         settings,
                                               const UpscalerEvaluateTextures& textures);
} // namespace vultra
