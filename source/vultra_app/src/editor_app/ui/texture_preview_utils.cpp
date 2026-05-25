#include "editor_app/ui/texture_preview_utils.hpp"

#include <vultra/core/rhi/structs/image_aspect.hpp>
#include <vultra/core/rhi/structs/pixel_format.hpp>

#include <algorithm>
#include <cctype>

namespace vultra_app::ui
{
    namespace
    {
        std::string lower(std::string_view text)
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        bool containsIgnoreCase(std::string_view haystack, std::string_view needle)
        {
            if (needle.empty())
                return true;
            return lower(haystack).find(lower(needle)) != std::string::npos;
        }
    } // namespace

    bool isDepthLikeTexture(const vultra::FrameGraphDebugTexture& texture)
    {
        if (containsIgnoreCase(texture.name, "ssao") || containsIgnoreCase(texture.resourceKey, "ssao") ||
            containsIgnoreCase(texture.name, "ambient occlusion") ||
            containsIgnoreCase(texture.resourceKey, "ambient occlusion"))
        {
            return false;
        }

        const auto aspect = vultra::rhi::getAspectMask(texture.format);
        return HasFlagValues(aspect, vultra::rhi::ImageAspectFlags::eDepth) ||
               containsIgnoreCase(texture.name, "depth") || containsIgnoreCase(texture.resourceKey, "depth") ||
               containsIgnoreCase(texture.name, "shadow") || containsIgnoreCase(texture.resourceKey, "shadow");
    }

    bool isShadowLikeTexture(const vultra::FrameGraphDebugTexture& texture)
    {
        return containsIgnoreCase(texture.name, "shadow") || containsIgnoreCase(texture.resourceKey, "shadow");
    }

    bool isNormalLikeTexture(const vultra::FrameGraphDebugTexture& texture)
    {
        return containsIgnoreCase(texture.name, "normal") || containsIgnoreCase(texture.resourceKey, "normal");
    }

    bool shouldGammaCorrectTexturePreview(const vultra::FrameGraphDebugTexture& texture)
    {
        if (isDepthLikeTexture(texture))
            return false;

        const auto key = lower(texture.name + " " + texture.resourceKey);
        if (isNormalLikeTexture(texture) || key.find("material") != std::string::npos ||
            key.find("roughness") != std::string::npos || key.find("metallic") != std::string::npos ||
            key.find("metalness") != std::string::npos || key.find("ao") != std::string::npos ||
            key.find("ssao") != std::string::npos || key.find("entity") != std::string::npos ||
            key.find("visibility") != std::string::npos)
        {
            return false;
        }

        return true;
    }

    int defaultTexturePreviewMode(const vultra::FrameGraphDebugTexture& texture)
    {
        if (isDepthLikeTexture(texture))
            return isShadowLikeTexture(texture) ? 1 : 2;
        if (isNormalLikeTexture(texture))
            return 5;
        return 0;
    }

    void normalizePreviewClamp(float& minValue, float& maxValue)
    {
        minValue = std::clamp(minValue, 0.0f, 1.0f);
        maxValue = std::clamp(maxValue, 0.0f, 1.0f);
        if (maxValue < minValue)
            std::swap(minValue, maxValue);
        if (maxValue <= minValue)
            maxValue = std::min(1.0f, minValue + 0.0001f);
    }

    float computeTextureFitScale(const ImVec2 available,
                                 const float  sourceWidth,
                                 const float  sourceHeight,
                                 const float  maxScale)
    {
        return std::clamp(std::min(available.x / std::max(sourceWidth, 1.0f),
                                   available.y / std::max(sourceHeight, 1.0f)),
                          0.05f,
                          maxScale);
    }

    vultra::FrameGraphTexturePreviewSettings makeFrameGraphTexturePreviewSettings(
        const std::string& selectedTextureKey,
        const bool         gammaCorrect,
        const bool         channels[4],
        const int          previewMode,
        const float        depthNear,
        const float        depthFar,
        const float        clampMin,
        const float        clampMax)
    {
        vultra::FrameGraphTexturePreviewSettings settings {};
        settings.gammaCorrect = gammaCorrect;
        settings.previewMode = previewMode;
        settings.depthNear = depthNear;
        settings.depthFar = depthFar;
        settings.clampMin = clampMin;
        settings.clampMax = clampMax;
        for (int i = 0; i < 4; ++i)
            settings.channels[i] = channels[i];
        settings.selectedTextureKey = selectedTextureKey;
        return settings;
    }
} // namespace vultra_app::ui
