#pragma once

#include "editor_app/editor_context.hpp"

#include <vultra/function/services/render_service.hpp>

#include <imgui.h>

#include <string>

namespace vultra_app::ui
{
    bool isDepthLikeTexture(const vultra::FrameGraphDebugTexture& texture);
    bool isShadowLikeTexture(const vultra::FrameGraphDebugTexture& texture);
    bool isNormalLikeTexture(const vultra::FrameGraphDebugTexture& texture);
    bool shouldGammaCorrectTexturePreview(const vultra::FrameGraphDebugTexture& texture);
    int  defaultTexturePreviewMode(const vultra::FrameGraphDebugTexture& texture);
    void normalizePreviewClamp(float& minValue, float& maxValue);
    float computeTextureFitScale(ImVec2 available, float sourceWidth, float sourceHeight, float maxScale = 8.0f);
    bool drawSaveFrameGraphTexturePreviewButton(EditorContext&                         ctx,
                                                const vultra::FrameGraphDebugTexture& texture,
                                                const char*                           dialogKey);

    vultra::FrameGraphTexturePreviewSettings makeFrameGraphTexturePreviewSettings(
        const std::string& selectedTextureKey,
        bool               gammaCorrect,
        const bool         channels[4],
        int                previewMode,
        float              depthNear,
        float              depthFar,
        float              clampMin,
        float              clampMax,
        uint32_t           maxPreviewExtent = 0);
} // namespace vultra_app::ui
