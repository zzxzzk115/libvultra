#include "editor_app/ui/texture_preview_utils.hpp"

#include <vultra/core/rhi/structs/image_aspect.hpp>
#include <vultra/core/rhi/structs/pixel_format.hpp>
#include <vultra/function/services/render_backend_service.hpp>

#include <IconsMaterialDesignIcons.h>
#include <ImGuiFileDialog/ImGuiFileDialog.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

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

        std::filesystem::path ensurePngExtension(std::filesystem::path path)
        {
            if (path.extension().empty())
                path.replace_extension(".png");
            return path;
        }

        std::string sanitizedFileName(std::string_view name)
        {
            std::string out;
            out.reserve(name.size());
            for (const char c : name)
            {
                const bool invalid = c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
                                     c == '|' || c == '?' || c == '*';
                out.push_back(invalid ? '_' : c);
            }
            return out.empty() ? std::string {"frame_graph_texture"} : out;
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
        return std::clamp(
            std::min(available.x / std::max(sourceWidth, 1.0f), available.y / std::max(sourceHeight, 1.0f)),
            0.05f,
            maxScale);
    }

    bool drawSaveFrameGraphTexturePreviewButton(EditorContext&                        ctx,
                                                const vultra::FrameGraphDebugTexture& texture,
                                                const char*                           dialogKey)
    {
        bool saved = false;
        if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save Preview"))
        {
            IGFD::FileDialogConfig config;
            config.path     = ".";
            config.fileName = sanitizedFileName(texture.name) + ".png";
            config.flags    = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_HideColumnType |
                           ImGuiFileDialogFlags_HideColumnSize | ImGuiFileDialogFlags_HideColumnDate |
                           ImGuiFileDialogFlags_DontShowHiddenFiles |
                           ImGuiFileDialogFlags_CaseInsensitiveExtentionFiltering |
                           ImGuiFileDialogFlags_NaturalSorting | ImGuiFileDialogFlags_DisableThumbnailMode;
            ImGuiFileDialog::Instance()->OpenDialog(dialogKey, "Save Texture Preview", ".png", config);
        }

        if (ImGuiFileDialog::Instance()->Display(
                dialogKey, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings, ImVec2 {520.0f, 360.0f}))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
                const auto path      = ensurePngExtension(
                    std::filesystem::path(ImGuiFileDialog::Instance()->GetFilePathName(IGFD_ResultMode_KeepInputFile)));
                saved = backendService && texture.texture &&
                        backendService->renderDevice().saveTextureToFile(
                            *texture.texture, path.generic_string(), vultra::rhi::ImageAspect::eColor);
                ctx.state.statusMessage = saved ? "Saved texture preview: " + path.generic_string() :
                                                  "Failed to save texture preview: " + path.generic_string();
            }
            ImGuiFileDialog::Instance()->Close();
        }
        return saved;
    }

    vultra::FrameGraphTexturePreviewSettings makeFrameGraphTexturePreviewSettings(const std::string& selectedTextureKey,
                                                                                  const bool         gammaCorrect,
                                                                                  const bool         channels[4],
                                                                                  const int          previewMode,
                                                                                  const float        depthNear,
                                                                                  const float        depthFar,
                                                                                  const float        clampMin,
                                                                                  const float        clampMax,
                                                                                  const uint32_t     maxPreviewExtent)
    {
        vultra::FrameGraphTexturePreviewSettings settings {};
        settings.gammaCorrect = gammaCorrect;
        settings.previewMode  = previewMode;
        settings.depthNear    = depthNear;
        settings.depthFar     = depthFar;
        settings.clampMin     = clampMin;
        settings.clampMax     = clampMax;
        settings.maxPreviewExtent = maxPreviewExtent;
        for (int i = 0; i < 4; ++i)
            settings.channels[i] = channels[i];
        settings.selectedTextureKey = selectedTextureKey;
        return settings;
    }
} // namespace vultra_app::ui
