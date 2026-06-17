#include "vultra/function/rendering/render_system.hpp"
#include "vultra/function/material_graph/material_graph_compiler.hpp"
#include "vultra/function/rendering/render_system_internal.hpp"     // material cooking moved to material_cook.cpp
#include "vultra/function/rendering/render_world_cook_internal.hpp" // render-world cook moved to render_world_cook.cpp

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/i18n/i18n.hpp"
#include "vultra/function/debug_draw/debug_draw_interface.hpp"

#include <glm/gtc/type_ptr.hpp>
#include "vultra/core/math/math.hpp"
#include "vultra/core/services/timing_service.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_command_buffer_access.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/deferred_deletion_queue.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/asset/builtin_assets.hpp"
#include "vultra/function/material/material_asset.hpp"
#include "vultra/function/material/material_params.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/framework/resource_uploader.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/rendering/srp/builtin/upload_resources.hpp"
#include "vultra/function/rendering/srp/declarative_renderer.hpp"
#include "vultra/function/rendering/srp/render_context.hpp"
#include "vultra/function/resource/gpu_vertex_layout.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/frame_debugger_service.hpp"
#include "vultra/function/services/gpu_resource_service.hpp"
#include "vultra/function/services/imgui_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/render_upscaler_service.hpp"
#include "vultra/function/services/shader_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/environment_component.hpp"
#include "vultra/function/world/components/gaussian_splat_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/layer_component.hpp"
#include "vultra/function/world/components/light_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/hierarchy_component.hpp"
#include "vultra/function/world/components/skin_palette_component.hpp"
#include "vultra/function/world/components/reflection_probe_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/components/ui_components.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/packing.hpp>

#include <vbase/core/exe_path.hpp>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>
#include <fg/GraphvizWriter.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#ifndef NDEBUG
#include <fstream>
#endif

namespace vultra
{
    // Material cooking helpers live in material_cook.cpp; bring the few entry points this file
    // calls into scope so the existing unqualified call sites keep resolving.
    using namespace rsdetail;

    namespace
    {
        void importPreparedFrameGraphUniforms(FrameGraph& fg, FrameRenderData& frameData, ViewRenderData& viewData)
        {
            if (frameData.frameData.frameBlock.buffer)
            {
                frameData.frameData.frameBlock.fgResource =
                    framegraph::importBuffer(fg,
                                             "FrameBlock",
                                             frameData.frameData.frameBlock.buffer,
                                             framegraph::BufferType::eUniformBuffer,
                                             sizeof(GPUFrameBlock));
            }
            if (viewData.cameraData.cameraBlock.buffer)
            {
                viewData.cameraData.cameraBlock.fgResource =
                    framegraph::importBuffer(fg,
                                             "CameraBlock",
                                             viewData.cameraData.cameraBlock.buffer,
                                             framegraph::BufferType::eUniformBuffer,
                                             sizeof(GPUCameraBlock));
            }
            if (viewData.cameraData.stereoCameraBlock.buffer)
            {
                viewData.cameraData.stereoCameraBlock.fgResource =
                    framegraph::importBuffer(fg,
                                             "StereoCameraBlock",
                                             viewData.cameraData.stereoCameraBlock.buffer,
                                             framegraph::BufferType::eUniformBuffer,
                                             sizeof(GPUStereoCameraBlock));
            }
        }


        [[nodiscard]] nlohmann::json materialPropertyBlockToJson(const std::vector<MaterialPropertyBlockEntry>& properties)
        {
            nlohmann::json json = nlohmann::json::object();
            for (const auto& property : properties)
            {
                if (property.name.empty())
                    continue;
                switch (property.type)
                {
                    case MaterialPropertyBlockValueType::eColor:
                        json[property.name] = nlohmann::json::array(
                            {property.colorValue.x, property.colorValue.y, property.colorValue.z, property.colorValue.w});
                        break;
                    case MaterialPropertyBlockValueType::eTexture2D:
                        json[property.name] = property.textureUri;
                        break;
                    case MaterialPropertyBlockValueType::eFloat:
                    default:
                        json[property.name] = property.floatValue;
                        break;
                }
            }
            return json;
        }

    } // namespace

    namespace
    {
        thread_local rhi::BuiltinProfilerGpuScopeContext g_CurrentBuiltinProfilerGpuScopeContext {};

        [[nodiscard]] constexpr bool isTrackyGpuProfilerEnabled()
        {
#if defined(TRACKY_ENABLE) && TRACKY_ENABLE
            return true;
#else
            return false;
#endif
        }

        [[nodiscard]] std::string rendererKeyFromRenderGraphUri(std::string_view uri)
        {
            auto filename = std::filesystem::path(std::string(uri)).filename().generic_string();
            constexpr std::array<std::string_view, 2> suffixes {".vrg.json", ".vrp.lua"};
            for (const auto suffix : suffixes)
            {
                if (filename.ends_with(suffix))
                {
                    filename.resize(filename.size() - suffix.size());
                    break;
                }
            }
            if (filename.empty())
                filename = "custom";
            for (auto& ch : filename)
            {
                if (ch == '-' || ch == ' ')
                    ch = '_';
                else
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return filename;
        }

        [[nodiscard]] bool rendererKeyRequiresRayTracingScene(std::string_view rendererKey)
        {
            return rendererKey == "universal_rt" || rendererKey == "default_rt";
        }

        void expandBounds(RenderWorld& out, const glm::vec3& point)
        {
            if (!out.hasBounds)
            {
                out.hasBounds = true;
                out.boundsMin = point;
                out.boundsMax = point;
                return;
            }
            out.boundsMin = glm::min(out.boundsMin, point);
            out.boundsMax = glm::max(out.boundsMax, point);
        }

        void expandBounds(RenderWorld& out, const glm::mat4& model, const glm::vec3& center, const float radius)
        {
            const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(center, 1.0f));
            const float     maxScale    = std::max({
                glm::length(glm::vec3(model[0])),
                glm::length(glm::vec3(model[1])),
                glm::length(glm::vec3(model[2])),
            });
            const glm::vec3 r(std::max(radius, 0.0f) * maxScale);
            expandBounds(out, worldCenter - r);
            expandBounds(out, worldCenter + r);
        }

        [[nodiscard]] bool uiVisible(const entt::registry& reg, entt::entity entity)
        {
            if (const auto* status = reg.try_get<EntityStatusComponent>(entity))
                return status->active && status->visible;
            return true;
        }

        [[nodiscard]] uint32_t entityLayerMask(const entt::registry& reg,
                                               const entt::entity    entity,
                                               const uint32_t        fallbackMask)
        {
            if (const auto* layer = reg.try_get<LayerComponent>(entity))
                return renderLayerMask(layer->layer);
            return fallbackMask;
        }

        [[nodiscard]] std::optional<uint32_t> uiTextureIndex(IAssetService& assets, const CoreUUID& texture)
        {
            if (!texture.valid())
                return std::nullopt;
            auto handle = assets.loadTextureAsync(texture);
            if (!handle.ready())
                return std::nullopt;
            return handle.gpuIndex();
        }

        // The font a UiTextComponent should render with: its assigned font, or the default builtin.
        [[nodiscard]] CoreUUID effectiveUiFontUuid(const CoreUUID& font)
        {
            return font.valid() ? font : builtinFontUuidForUri(kBuiltinDefaultFontUri);
        }

        // Resolve raw font bytes (a .ttf/.otf the GlyphAtlas can hand to FreeType) for a font UUID.
        // Works for project font assets (source path) and builtin fonts (builtin:// pack).
        [[nodiscard]] std::vector<std::byte> resolveUiFontBytes(IAssetService& assets, const CoreUUID& font)
        {
            const CoreUUID effective = effectiveUiFontUuid(font);
            std::string    uri;
            if (!assets.resolveAssetUri(effective, uri) || uri.empty())
                uri = std::string(kBuiltinDefaultFontUri);
            auto r = assets.loadBinaryAssetSync(uri);
            if (!r)
                return {};
            const auto&            bytes = r.value();
            std::vector<std::byte> out(bytes.size());
            std::memcpy(out.data(), bytes.data(), bytes.size());
            return out;
        }

        // Minimal UTF-8 decoder: appends code points, substituting U+FFFD on malformed input.
        void decodeUtf8(const std::string& s, std::vector<uint32_t>& out)
        {
            const size_t n = s.size();
            size_t       i = 0;
            while (i < n)
            {
                const unsigned char c     = static_cast<unsigned char>(s[i]);
                uint32_t            cp     = 0;
                size_t              extra  = 0;
                if (c < 0x80u) { cp = c; extra = 0; }
                else if ((c >> 5) == 0x6u) { cp = c & 0x1Fu; extra = 1; }
                else if ((c >> 4) == 0xEu) { cp = c & 0x0Fu; extra = 2; }
                else if ((c >> 3) == 0x1Eu) { cp = c & 0x07u; extra = 3; }
                else { out.push_back(0xFFFDu); ++i; continue; }

                if (i + extra >= n) { out.push_back(0xFFFDu); break; }
                bool ok = true;
                for (size_t k = 1; k <= extra; ++k)
                {
                    const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                    if ((cc >> 6) != 0x2u) { ok = false; break; }
                    cp = (cp << 6) | (cc & 0x3Fu);
                }
                if (!ok) { out.push_back(0xFFFDu); ++i; continue; }
                out.push_back(cp);
                i += extra + 1;
            }
        }

        void resolveUiRectTopLeft(const RectTransformComponent& rect,
                                  const glm::vec2&             parentMin,
                                  const glm::vec2&             parentSize,
                                  glm::vec2&                   outMinPx,
                                  glm::vec2&                   outSizePx)
        {
            const glm::vec2 anchorMin = parentMin + parentSize * rect.anchorMin;
            const glm::vec2 anchorMax = parentMin + parentSize * rect.anchorMax;
            outSizePx                 = (anchorMax - anchorMin) + rect.sizeDeltaPx;
            outMinPx                  = anchorMin + rect.anchoredPositionPx - outSizePx * rect.pivot;
        }

        void cookUiChildren(World&                 world,
                            IAssetService&         assets,
                            RenderWorld&           out,
                            entt::entity           entity,
                            const glm::vec2&       parentMin,
                            const glm::vec2&       parentSize,
                            const CanvasComponent& canvas,
                            const glm::mat4&       canvasWorldMatrix,
                            const int              sortOrder,
                            const uint32_t         depth,
                            rendering::GlyphAtlas* glyphAtlas,
                            const uint32_t         glyphAtlasIndex,
                            const glm::vec2&       clipMin,
                            const glm::vec2&       clipMax)
        {
            auto& reg = world.registry();
            auto* rect = reg.try_get<RectTransformComponent>(entity);
            if (!rect || !uiVisible(reg, entity))
                return;

            glm::vec2 minPx {};
            glm::vec2 sizePx {};
            resolveUiRectTopLeft(*rect, parentMin, parentSize, minPx, sizePx);
            const glm::vec2 maxPx     = minPx + sizePx * rect->scale;
            uint32_t        localDrawOrder {0u};
            int             overlayBias {0}; // added to sortOrder so an expanded dropdown list draws on top

            // CPU-side clip: clamp a draw rect (and proportionally its UVs) to the active clip
            // rect, returning false if fully outside. A huge default clip is a no-op; scroll views
            // tighten it for their descendants.
            const auto applyClip = [&](glm::vec2& dMin, glm::vec2& dMax, glm::vec2& uvMin, glm::vec2& uvMax) -> bool {
                const glm::vec2 cMin = glm::max(dMin, clipMin);
                const glm::vec2 cMax = glm::min(dMax, clipMax);
                if (cMax.x <= cMin.x || cMax.y <= cMin.y)
                    return false;
                if (cMin != dMin || cMax != dMax)
                {
                    const glm::vec2 size = glm::max(dMax - dMin, glm::vec2 {1e-4f});
                    const glm::vec2 t0   = (cMin - dMin) / size;
                    const glm::vec2 t1   = (cMax - dMin) / size;
                    const glm::vec2 u0   = uvMin;
                    const glm::vec2 u1   = uvMax;
                    uvMin = u0 + (u1 - u0) * t0;
                    uvMax = u0 + (u1 - u0) * t1;
                    dMin  = cMin;
                    dMax  = cMax;
                }
                return true;
            };

            const auto pushRectItem = [&](const glm::vec2& drawMin,
                                          const glm::vec2& drawMax,
                                          const glm::vec4& color,
                                          const uint32_t textureIndex,
                                          const uint32_t flags,
                                          const uint32_t fitMode) {
                RenderUiDrawItem item {};
                if (const auto* id = reg.try_get<IDComponent>(entity))
                    item.entity = id->uuid;
                item.rectMinPx         = drawMin;
                item.rectMaxPx         = drawMax;
                item.canvasReferencePx = glm::max(canvas.referenceResolutionPx, glm::vec2 {1.0f});
                item.color             = color;
                item.textureIndex      = textureIndex;
                item.flags             = flags;
                item.scaleMode         = canvas.scaleMode;
                item.fitMode           = fitMode;
                item.sortOrder         = sortOrder + overlayBias;
                item.depth             = depth * 16u + localDrawOrder++;
                item.layerMask         = entityLayerMask(reg, entity, kRenderLayerUiMask);
                item.space             = canvas.renderMode == 1u ? 1u : 0u;
                item.pixelsPerUnit     = canvas.pixelsPerUnit > 0.0f ? canvas.pixelsPerUnit : 250.0f;
                item.worldMatrix       = canvasWorldMatrix;
                if (!applyClip(item.rectMinPx, item.rectMaxPx, item.uvMin, item.uvMax))
                    return;
                out.uiDrawItems.push_back(item);
            };
            const auto pushItem = [&](const glm::vec4& color,
                                      const uint32_t textureIndex,
                                      const uint32_t flags,
                                      const uint32_t fitMode) {
                pushRectItem(minPx, maxPx, color, textureIndex, flags, fitMode);
            };

            // A coverage glyph quad with explicit color + atlas UVs (for widgets that lay out
            // their own text, e.g. the input field). flags = textured(1) | coverage(2).
            const auto pushGlyphRect = [&](const glm::vec2& drawMin,
                                           const glm::vec2& drawMax,
                                           const glm::vec2& uvMin,
                                           const glm::vec2& uvMax,
                                           const glm::vec4& color) {
                RenderUiDrawItem item {};
                if (const auto* id = reg.try_get<IDComponent>(entity))
                    item.entity = id->uuid;
                item.rectMinPx         = drawMin;
                item.rectMaxPx         = drawMax;
                item.canvasReferencePx = glm::max(canvas.referenceResolutionPx, glm::vec2 {1.0f});
                item.color             = color;
                item.uvMin             = uvMin;
                item.uvMax             = uvMax;
                item.textureIndex      = glyphAtlasIndex;
                item.flags             = 3u;
                item.scaleMode         = canvas.scaleMode;
                item.fitMode           = 0u;
                item.sortOrder         = sortOrder + overlayBias;
                item.depth             = depth * 16u + localDrawOrder++;
                item.layerMask         = entityLayerMask(reg, entity, kRenderLayerUiMask);
                item.space             = canvas.renderMode == 1u ? 1u : 0u;
                item.pixelsPerUnit     = canvas.pixelsPerUnit > 0.0f ? canvas.pixelsPerUnit : 250.0f;
                item.worldMatrix       = canvasWorldMatrix;
                if (!applyClip(item.rectMinPx, item.rectMaxPx, item.uvMin, item.uvMax))
                    return;
                out.uiDrawItems.push_back(item);
            };

            const auto normalizedRangeValue = [](float value, const float minValue, const float maxValue) {
                if (maxValue <= minValue)
                    return 0.0f;
                return std::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
            };

            const auto buttonStateColor = [](const UiButtonComponent& button) {
                if (button.pressed)
                    return button.pressedColor;
                if (button.hovered)
                    return button.hoveredColor;
                return button.normalColor;
            };
            const auto isButtonTargetGraphic = [&](const UiButtonComponent& button) {
                if (!button.targetGraphic.valid())
                    return true;
                const auto* id = reg.try_get<IDComponent>(entity);
                return id && id->uuid == button.targetGraphic;
            };
            const auto targetGraphicButtonColor = [&]() -> std::optional<glm::vec4> {
                for (auto buttonEntity : reg.view<UiButtonComponent>())
                {
                    const auto& button = reg.get<UiButtonComponent>(buttonEntity);
                    if (!button.enabled)
                        continue;
                    if (!button.targetGraphic.valid())
                    {
                        if (buttonEntity == entity)
                            return buttonStateColor(button);
                        continue;
                    }
                    if (const auto* id = reg.try_get<IDComponent>(entity); id && id->uuid == button.targetGraphic)
                        return buttonStateColor(button);
                }
                return std::nullopt;
            };

            if (const auto* button = reg.try_get<UiButtonComponent>(entity); button && button->enabled)
            {
                const auto* image = reg.try_get<UiImageComponent>(entity);
                const auto* panel = reg.try_get<UiPanelComponent>(entity);
                if ((!image || !image->enabled) && (!panel || !panel->enabled) && isButtonTargetGraphic(*button))
                    pushItem(buttonStateColor(*button), 0u, 0u, 0u);
            }
            if (const auto* panel = reg.try_get<UiPanelComponent>(entity); panel && panel->enabled)
            {
                pushItem(panel->color * targetGraphicButtonColor().value_or(glm::vec4 {1.0f}), 0u, 0u, 0u);
            }

            if (const auto* toggle = reg.try_get<UiToggleComponent>(entity); toggle && toggle->enabled)
            {
                pushItem(toggle->checked ? toggle->onColor : toggle->offColor, 0u, 0u, 0u);
                if (toggle->checked)
                {
                    const glm::vec2 inset = glm::max((maxPx - minPx) * 0.22f, glm::vec2 {4.0f});
                    pushRectItem(minPx + inset, maxPx - inset, toggle->checkColor, 0u, 0u, 0u);
                }
            }

            if (const auto* progress = reg.try_get<UiProgressBarComponent>(entity); progress && progress->enabled)
            {
                const float t = normalizedRangeValue(progress->value, progress->minValue, progress->maxValue);
                pushItem(progress->trackColor, 0u, 0u, 0u);
                if (t > 0.0f)
                {
                    const glm::vec2 fillMax {minPx.x + (maxPx.x - minPx.x) * t, maxPx.y};
                    pushRectItem(minPx, fillMax, progress->fillColor, 0u, 0u, 0u);
                }
            }

            if (const auto* slider = reg.try_get<UiSliderComponent>(entity); slider && slider->enabled)
            {
                const float t = normalizedRangeValue(slider->value, slider->minValue, slider->maxValue);
                const float height = std::max(maxPx.y - minPx.y, 1.0f);
                const float trackInsetY = std::max(height * 0.35f, 2.0f);
                const glm::vec2 trackMin {minPx.x, minPx.y + trackInsetY};
                const glm::vec2 trackMax {maxPx.x, maxPx.y - trackInsetY};
                pushRectItem(trackMin, trackMax, slider->trackColor, 0u, 0u, 0u);
                if (t > 0.0f)
                    pushRectItem(trackMin, {trackMin.x + (trackMax.x - trackMin.x) * t, trackMax.y}, slider->fillColor, 0u, 0u, 0u);
                const float handleRadius = std::min(std::max(height * 0.45f, 6.0f), std::max((maxPx.x - minPx.x) * 0.12f, 6.0f));
                const float handleX = minPx.x + (maxPx.x - minPx.x) * t;
                pushRectItem({handleX - handleRadius, minPx.y},
                             {handleX + handleRadius, maxPx.y},
                             slider->handleColor,
                             0u,
                             0u,
                             0u);
            }

            if (const auto* image = reg.try_get<UiImageComponent>(entity); image && image->enabled)
            {
                const auto textureIndex = uiTextureIndex(assets, image->texture);
                glm::vec4 color = image->tint * targetGraphicButtonColor().value_or(glm::vec4 {1.0f});
                pushItem(color, textureIndex.value_or(0u), textureIndex ? 1u : 0u, image->fitMode);
            }

            if (const auto* textC = reg.try_get<UiTextComponent>(entity);
                textC && textC->enabled && glyphAtlas && glyphAtlasIndex != 0u &&
                (!textC->text.empty() || !textC->localizationKey.empty()))
            {
                // Resolve i18n: a non-empty localizationKey overrides the literal text.
                const std::string displayText =
                    textC->localizationKey.empty() ? textC->text : std::string(vultra::tr(textC->localizationKey));
                const uint32_t pixelSize =
                    static_cast<uint32_t>(std::clamp(std::lround(textC->fontSizePx), 1L, 256L));
                const uint64_t fontKey = std::hash<CoreUUID> {}(effectiveUiFontUuid(textC->font));

                if (glyphAtlas->ensureFont(fontKey, [&] { return resolveUiFontBytes(assets, textC->font); }))
                {
                    // Glyph quad with the atlas sub-rect; flags = textured(1) | coverage(2).
                    const auto pushGlyphItem = [&](const glm::vec2& drawMin,
                                                   const glm::vec2& drawMax,
                                                   const glm::vec2& uvMin,
                                                   const glm::vec2& uvMax) {
                        RenderUiDrawItem item {};
                        if (const auto* id = reg.try_get<IDComponent>(entity))
                            item.entity = id->uuid;
                        item.rectMinPx         = drawMin;
                        item.rectMaxPx         = drawMax;
                        item.canvasReferencePx = glm::max(canvas.referenceResolutionPx, glm::vec2 {1.0f});
                        item.color             = textC->color;
                        item.uvMin             = uvMin;
                        item.uvMax             = uvMax;
                        item.textureIndex      = glyphAtlasIndex;
                        item.flags             = 3u; // textured | coverage glyph
                        item.scaleMode         = canvas.scaleMode;
                        item.fitMode           = 0u;
                        item.sortOrder         = sortOrder;
                        item.depth             = depth * 16u + localDrawOrder++;
                        item.layerMask         = entityLayerMask(reg, entity, kRenderLayerUiMask);
                        item.space             = canvas.renderMode == 1u ? 1u : 0u;
                        item.pixelsPerUnit     = canvas.pixelsPerUnit > 0.0f ? canvas.pixelsPerUnit : 250.0f;
                        item.worldMatrix       = canvasWorldMatrix;
                        if (!applyClip(item.rectMinPx, item.rectMaxPx, item.uvMin, item.uvMax))
                            return;
                        out.uiDrawItems.push_back(item);
                    };

                    float ascentPx = static_cast<float>(pixelSize);
                    float lineHeightPx = static_cast<float>(pixelSize);
                    glyphAtlas->fontMetrics(fontKey, pixelSize, ascentPx, lineHeightPx);

                    std::vector<uint32_t> codepoints;
                    decodeUtf8(displayText, codepoints);

                    // Single-line layout: measure advance, then place by h/v alignment in the rect.
                    float totalAdvance = 0.0f;
                    for (const uint32_t cp : codepoints)
                        if (const auto* g = glyphAtlas->getGlyph(fontKey, pixelSize, cp))
                            totalAdvance += g->advancePx;

                    const float boxW = maxPx.x - minPx.x;
                    const float boxH = maxPx.y - minPx.y;
                    float       penX = minPx.x;
                    if (textC->horizontalAlign == 1u)
                        penX += (boxW - totalAdvance) * 0.5f;
                    else if (textC->horizontalAlign == 2u)
                        penX += (boxW - totalAdvance);

                    float baselineY;
                    if (textC->verticalAlign == 0u)
                        baselineY = minPx.y + ascentPx;
                    else if (textC->verticalAlign == 2u)
                        baselineY = maxPx.y - (lineHeightPx - ascentPx);
                    else
                        baselineY = minPx.y + (boxH - lineHeightPx) * 0.5f + ascentPx;

                    for (const uint32_t cp : codepoints)
                    {
                        const auto* g = glyphAtlas->getGlyph(fontKey, pixelSize, cp);
                        if (!g)
                            continue;
                        if (g->hasBitmap)
                        {
                            const glm::vec2 gMin {penX + g->bearingPx.x, baselineY - g->bearingPx.y};
                            const glm::vec2 gMax {gMin.x + g->sizePx.x, gMin.y + g->sizePx.y};
                            pushGlyphItem(gMin, gMax, g->uvMin, g->uvMax);
                        }
                        penX += g->advancePx;
                    }
                }
            }

            if (const auto* field = reg.try_get<UiInputFieldComponent>(entity);
                field && field->enabled && glyphAtlas && glyphAtlasIndex != 0u)
            {
                pushItem(field->focused ? field->focusedColor : field->normalColor, 0u, 0u, 0u);

                const bool         showPlaceholder = field->text.empty() && !field->focused;
                const std::string& shown           = showPlaceholder ? field->placeholder : field->text;
                const glm::vec4&    glyphColor      = showPlaceholder ? field->placeholderColor : field->textColor;

                const uint32_t pixelSize = static_cast<uint32_t>(std::clamp(std::lround(field->fontSizePx), 1L, 256L));
                const uint64_t fontKey   = std::hash<CoreUUID> {}(effectiveUiFontUuid(field->font));
                if (glyphAtlas->ensureFont(fontKey, [&] { return resolveUiFontBytes(assets, field->font); }))
                {
                    float ascentPx = static_cast<float>(pixelSize);
                    float lineHeightPx = static_cast<float>(pixelSize);
                    glyphAtlas->fontMetrics(fontKey, pixelSize, ascentPx, lineHeightPx);

                    const float pad       = 6.0f;
                    const float innerMinX = minPx.x + pad;
                    const float innerMaxX = maxPx.x - pad;
                    const float baselineY = minPx.y + ((maxPx.y - minPx.y) - lineHeightPx) * 0.5f + ascentPx;

                    // Horizontal scroll so the caret stays in view when text overflows.
                    float caretAdvance = 0.0f;
                    if (field->focused)
                    {
                        std::vector<uint32_t> pre;
                        const int caret = std::clamp(field->caret, 0, static_cast<int>(shown.size()));
                        decodeUtf8(shown.substr(0, static_cast<size_t>(caret)), pre);
                        for (const uint32_t cp : pre)
                            if (const auto* g = glyphAtlas->getGlyph(fontKey, pixelSize, cp))
                                caretAdvance += g->advancePx;
                    }
                    const float innerWidth   = std::max(innerMaxX - innerMinX, 1.0f);
                    const float scrollOffset = std::max(0.0f, caretAdvance - innerWidth);

                    std::vector<uint32_t> codepoints;
                    decodeUtf8(shown, codepoints);
                    float penX = innerMinX - scrollOffset;
                    for (const uint32_t cp : codepoints)
                    {
                        const auto* g = glyphAtlas->getGlyph(fontKey, pixelSize, cp);
                        if (!g)
                            continue;
                        if (g->hasBitmap)
                        {
                            const glm::vec2 gMin {penX + g->bearingPx.x, baselineY - g->bearingPx.y};
                            const glm::vec2 gMax {gMin.x + g->sizePx.x, gMin.y + g->sizePx.y};
                            if (gMax.x > innerMinX && gMin.x < innerMaxX) // clip overflow
                                pushGlyphRect(gMin, gMax, g->uvMin, g->uvMax, glyphColor);
                        }
                        penX += g->advancePx;
                    }
                    if (field->focused)
                    {
                        const float caretX = innerMinX - scrollOffset + caretAdvance;
                        if (caretX >= innerMinX - 1.0f && caretX <= innerMaxX + 1.0f)
                            pushRectItem({caretX, minPx.y + pad * 0.5f},
                                         {caretX + 2.0f, maxPx.y - pad * 0.5f},
                                         field->caretColor,
                                         0u,
                                         0u,
                                         0u);
                    }
                }
            }

            if (const auto* dd = reg.try_get<UiDropdownComponent>(entity);
                dd && dd->enabled && glyphAtlas && glyphAtlasIndex != 0u)
            {
                pushItem(dd->expanded ? dd->hoveredColor : dd->normalColor, 0u, 0u, 0u);

                const uint32_t pixelSize = static_cast<uint32_t>(std::clamp(std::lround(dd->fontSizePx), 1L, 256L));
                const uint64_t fontKey   = std::hash<CoreUUID> {}(effectiveUiFontUuid(dd->font));
                const bool     haveFont  = glyphAtlas->ensureFont(fontKey, [&] { return resolveUiFontBytes(assets, dd->font); });
                float          ascentPx  = static_cast<float>(pixelSize);
                float          lineHeightPx = static_cast<float>(pixelSize);
                if (haveFont)
                    glyphAtlas->fontMetrics(fontKey, pixelSize, ascentPx, lineHeightPx);

                // Single-line, left-aligned, clipped text inside a rect.
                const auto drawLine = [&](const glm::vec2& rmin, const glm::vec2& rmax, const std::string& s, const glm::vec4& col) {
                    if (!haveFont || s.empty())
                        return;
                    const float pad       = 6.0f;
                    const float innerMinX = rmin.x + pad;
                    const float innerMaxX = rmax.x - pad;
                    const float baselineY = rmin.y + ((rmax.y - rmin.y) - lineHeightPx) * 0.5f + ascentPx;
                    std::vector<uint32_t> cps;
                    decodeUtf8(s, cps);
                    float penX = innerMinX;
                    for (const uint32_t cp : cps)
                    {
                        const auto* g = glyphAtlas->getGlyph(fontKey, pixelSize, cp);
                        if (!g)
                            continue;
                        if (g->hasBitmap)
                        {
                            const glm::vec2 gMin {penX + g->bearingPx.x, baselineY - g->bearingPx.y};
                            const glm::vec2 gMax {gMin.x + g->sizePx.x, gMin.y + g->sizePx.y};
                            if (gMax.x > innerMinX && gMin.x < innerMaxX)
                                pushGlyphRect(gMin, gMax, g->uvMin, g->uvMax, col);
                        }
                        penX += g->advancePx;
                    }
                };

                const std::string headerText =
                    (dd->selectedIndex >= 0 && dd->selectedIndex < static_cast<int>(dd->options.size())) ?
                        dd->options[static_cast<size_t>(dd->selectedIndex)] :
                        std::string {};
                drawLine(minPx, maxPx, headerText, dd->textColor);

                if (dd->expanded && !dd->options.empty())
                {
                    overlayBias          = 1 << 20; // draw the option list above sibling UI
                    const float rowH     = std::max(maxPx.y - minPx.y, 1.0f);
                    for (size_t i = 0; i < dd->options.size(); ++i)
                    {
                        const glm::vec2 rmin {minPx.x, maxPx.y + rowH * static_cast<float>(i)};
                        const glm::vec2 rmax {maxPx.x, rmin.y + rowH};
                        const bool      selected = static_cast<int>(i) == dd->selectedIndex;
                        pushRectItem(rmin, rmax, selected ? dd->selectedColor : dd->panelColor, 0u, 0u, 0u);
                        drawLine(rmin, rmax, dd->options[i], dd->textColor);
                    }
                    overlayBias = 0;
                }
            }

            // Scroll view: draw its background, then offset + clip its descendants.
            const auto* scrollView = reg.try_get<UiScrollViewComponent>(entity);
            if (scrollView && scrollView->enabled)
                pushItem(scrollView->backgroundColor, 0u, 0u, 0u);

            const glm::vec2 childParentMin =
                (scrollView && scrollView->enabled) ? minPx - scrollView->scrollPx : minPx;
            const glm::vec2 childClipMin = (scrollView && scrollView->enabled) ? glm::max(clipMin, minPx) : clipMin;
            const glm::vec2 childClipMax = (scrollView && scrollView->enabled) ? glm::min(clipMax, maxPx) : clipMax;

            const auto* layout = reg.try_get<UiLayoutComponent>(entity);
            uint32_t childIndex = 0u;
            for (auto child = world.firstChild(entity); child != entt::null; child = world.nextSibling(child))
            {
                if (layout && layout->enabled && layout->kind != 0u)
                {
                    auto* childRect = reg.try_get<RectTransformComponent>(child);
                    if (childRect)
                    {
                        const glm::vec2 innerMin = minPx + glm::vec2 {layout->paddingPx.x, layout->paddingPx.y};
                        const glm::vec2 innerMax = maxPx - glm::vec2 {layout->paddingPx.z, layout->paddingPx.w};
                        const glm::vec2 cell     = glm::max(layout->cellSizePx, glm::vec2 {1.0f});
                        if (layout->kind == 1u)
                            childRect->anchoredPositionPx = glm::vec2 {
                                layout->paddingPx.x + childIndex * (layout->cellSizePx.x + layout->spacingPx),
                                layout->paddingPx.y};
                        else if (layout->kind == 2u)
                            childRect->anchoredPositionPx = glm::vec2 {
                                layout->paddingPx.x,
                                layout->paddingPx.y + childIndex * (layout->cellSizePx.y + layout->spacingPx)};
                        else if (layout->kind == 3u)
                        {
                            const uint32_t columns =
                                std::max(1u, static_cast<uint32_t>(std::floor((innerMax.x - innerMin.x) /
                                                                               std::max(cell.x + layout->spacingPx, 1.0f))));
                            childRect->anchoredPositionPx = glm::vec2 {
                                layout->paddingPx.x + (childIndex % columns) * (layout->cellSizePx.x + layout->spacingPx),
                                layout->paddingPx.y + (childIndex / columns) * (layout->cellSizePx.y + layout->spacingPx)};
                        }
                        childRect->anchorMin   = {0.0f, 0.0f};
                        childRect->anchorMax   = {0.0f, 0.0f};
                        childRect->pivot       = {0.0f, 0.0f};
                        childRect->sizeDeltaPx = layout->cellSizePx;
                    }
                }

                cookUiChildren(world,
                               assets,
                               out,
                               child,
                               childParentMin,
                               maxPx - minPx,
                               canvas,
                               canvasWorldMatrix,
                               sortOrder,
                               depth + 1u,
                               glyphAtlas,
                               glyphAtlasIndex,
                               childClipMin,
                               childClipMax);
                ++childIndex;
            }
        }

        void cookUi(World&                 world,
                    IAssetService&         assets,
                    RenderWorld&           out,
                    rendering::GlyphAtlas* glyphAtlas,
                    const uint32_t         glyphAtlasIndex)
        {
            auto& reg = world.registry();
            auto  canvasView = reg.view<CanvasComponent>();
            for (auto canvasEntity : canvasView)
            {
                const auto& canvas = canvasView.get<CanvasComponent>(canvasEntity);
                if (!canvas.enabled || !uiVisible(reg, canvasEntity))
                    continue;

                const glm::vec2 canvasSize = glm::max(canvas.referenceResolutionPx, glm::vec2 {1.0f});
                glm::mat4       canvasWorldMatrix {1.0f};
                if (canvas.renderMode == 1u)
                    if (const auto* tr = reg.try_get<TransformComponent>(canvasEntity))
                        canvasWorldMatrix = tr->worldMatrix;
                for (auto child = world.firstChild(canvasEntity); child != entt::null; child = world.nextSibling(child))
                    cookUiChildren(world,
                                   assets,
                                   out,
                                   child,
                                   {0.0f, 0.0f},
                                   canvasSize,
                                   canvas,
                                   canvasWorldMatrix,
                                   canvas.sortOrder,
                                   1u,
                                   glyphAtlas,
                                   glyphAtlasIndex,
                                   glm::vec2 {-1e9f, -1e9f},
                                   glm::vec2 {1e9f, 1e9f});
            }

            std::sort(out.uiDrawItems.begin(), out.uiDrawItems.end(), [](const RenderUiDrawItem& a, const RenderUiDrawItem& b) {
                if (a.sortOrder != b.sortOrder)
                    return a.sortOrder < b.sortOrder;
                return a.depth < b.depth;
            });
        }

        struct FrameGraphSnapshotWriter
        {
            nlohmann::json                          snapshot;
            std::unordered_map<std::string, size_t> emittedNodes;
            std::unordered_set<std::string>         emittedEdges;

            FrameGraphSnapshotWriter(std::string_view cameraName, std::string_view rendererKey, std::string dot)
            {
                snapshot["camera"]   = cameraName;
                snapshot["renderer"] = rendererKey;
                snapshot["dot"]      = std::move(dot);
                snapshot["nodes"]    = nlohmann::json::array();
                snapshot["edges"]    = nlohmann::json::array();
            }

            static std::string passId(const PassNode& pass) { return "pass:" + std::to_string(pass.getId()); }

            static std::string resourceId(const ResourceNode& resource)
            {
                return "resource:" + std::to_string(resource.getResourceId()) + "_v" +
                       std::to_string(resource.getVersion());
            }

            static const ResourceNode* findResource(const std::vector<ResourceNode>& resources, FrameGraphResource id)
            {
                const auto it = std::find_if(
                    resources.begin(), resources.end(), [&](const auto& resource) { return resource.getId() == id; });
                return it != resources.end() ? &*it : nullptr;
            }

            void emitNode(std::string id, std::string label, std::string kind, nlohmann::json extra = {})
            {
                if (id.empty())
                    return;

                if (auto it = emittedNodes.find(id); it != emittedNodes.end())
                {
                    auto& node = snapshot["nodes"][it->second];
                    if (!label.empty())
                        node["label"] = std::move(label);
                    if (!kind.empty())
                        node["kind"] = std::move(kind);
                    node.update(extra);
                    return;
                }

                nlohmann::json node {
                    {"id", id},
                    {"label", std::move(label)},
                    {"kind", std::move(kind)},
                };
                node.update(extra);
                emittedNodes.emplace(std::move(id), snapshot["nodes"].size());
                snapshot["nodes"].push_back(std::move(node));
            }

            void emitPass(const PassNode& pass)
            {
                emitNode(passId(pass),
                         std::string(pass.getName()),
                         "pass",
                         nlohmann::json {
                             {"sideEffect", pass.hasSideEffect()},
                             {"active", pass.canExecute()},
                         });
            }

            void emitResource(const ResourceNode& resource, std::optional<bool> imported = std::nullopt)
            {
                std::string label(resource.getName());
                if (resource.getVersion() > ResourceEntry::kInitialVersion)
                    label += " v" + std::to_string(resource.getVersion());

                nlohmann::json extra {
                    {"version", resource.getVersion()},
                    {"refCount", resource.getRefCount()},
                };
                if (imported)
                    extra["imported"] = *imported;

                emitNode(resourceId(resource), std::move(label), "resource", std::move(extra));
            }

            void emitEdge(std::string from, std::string to, std::string label)
            {
                if (from.empty() || to.empty() || from == to)
                    return;

                const std::string key = from + "->" + to + ":" + label;
                if (!emittedEdges.insert(key).second)
                    return;

                snapshot["edges"].push_back({
                    {"from", std::move(from)},
                    {"to", std::move(to)},
                    {"label", std::move(label)},
                });
            }

            void operator()(const PassNode& pass, const std::vector<ResourceNode>& resources)
            {
                if (!pass.canExecute())
                    return;

                emitPass(pass);
                const auto pid = passId(pass);

                for (const auto& access : pass.each(PassNode::Read {}))
                {
                    if (const auto* resource = findResource(resources, access.id))
                    {
                        emitResource(*resource);
                        emitEdge(resourceId(*resource), pid, "read");
                    }
                }
                for (const auto& access : pass.each(PassNode::Write {}))
                {
                    if (const auto* resource = findResource(resources, access.id))
                    {
                        emitResource(*resource);
                        emitEdge(pid, resourceId(*resource), "write");
                    }
                }
            }

            void operator()(const ResourceNode& resource, const ResourceEntry& entry, const std::vector<PassNode>&)
            {
                if (resource.getRefCount() > 0)
                    emitResource(resource, entry.isImported());
            }

            void flush(std::ostream& os) const { os << snapshot.dump(); }
        };

        void clearColorTarget(rhi::CommandBuffer&                   cb,
                              rhi::Texture&                         target,
                              const rhi::Rect2D&                    area,
                              const std::optional<rhi::ClearValue>& clearValue,
                              const bool                            enableMultiview,
                              const uint32_t                        multiviewMask)
        {
            rhi::FramebufferInfo clearFbInfo {
                .area             = area,
                .layers           = enableMultiview ? 2u : 1u,
                .viewMask         = enableMultiview ? multiviewMask : 0u,
                .colorAttachments = {rhi::AttachmentInfo {
                    .target = &target,
                    .clearValue =
                        clearValue.has_value() ? clearValue : std::optional<rhi::ClearValue> {glm::vec4 {0, 0, 0, 1}},
                }},
            };

            rhi::prepareForAttachment(cb, target, false);
            cb.beginRendering(clearFbInfo);
            cb.endRendering();
        }

        [[nodiscard]] glm::vec3 safeNormalizeDirection(const glm::vec3& direction, const glm::vec3& fallback)
        {
            const float len2 = glm::dot(direction, direction);
            return len2 > 1e-8f ? direction * glm::inversesqrt(len2) : fallback;
        }

        [[nodiscard]] bool supportsUpscalerOutputExtent(const rhi::Extent2D extent)
        {
            return extent.width >= 320u && extent.height >= 180u;
        }

        void finalizeRenderCamera(RenderCamera& cam)
        {
            cam.viewProjection        = cam.projection * cam.view;
            cam.inverseView           = glm::inverse(cam.view);
            cam.inverseProjection     = glm::inverse(cam.projection);
            cam.inverseViewProjection = glm::inverse(cam.viewProjection);

            auto planes = math::extractFrustumPlanes(cam.viewProjection);
            for (int i = 0; i < 6; ++i)
                cam.frustumPlanes[i] = glm::vec4(planes[i].normal, planes[i].d);
        }

        [[nodiscard]] RenderCamera cameraForRenderExtent(const RenderCamera& src, const rhi::Extent2D extent)
        {
            RenderCamera cam = src;
            if (cam.isXRView)
                return cam;

            const float aspect =
                static_cast<float>(std::max(extent.width, 1u)) / static_cast<float>(std::max(extent.height, 1u));
            if (std::abs(cam.projection[3][3]) < 1e-5f)
            {
                cam.projection = glm::perspectiveRH_ZO(cam.fovY, std::max(aspect, 0.0001f), cam.zNear, cam.zFar);
                finalizeRenderCamera(cam);
            }
            else
            {
                const float orthoHeight = cam.projection[1][1] != 0.0f ? std::abs(2.0f / cam.projection[1][1]) : 1.0f;
                const float orthoWidth  = orthoHeight * std::max(aspect, 0.0001f);
                cam.projection          = glm::orthoRH_ZO(-orthoWidth * 0.5f,
                                                 orthoWidth * 0.5f,
                                                 -orthoHeight * 0.5f,
                                                 orthoHeight * 0.5f,
                                                 cam.zNear,
                                                 cam.zFar);
                finalizeRenderCamera(cam);
            }
            if (cam.hasPreviousViewProjection)
            {
                // The previous projection was cooked for the backbuffer aspect; rebuild it for the
                // render extent so reprojection (motion vectors, upscalers) compares matching frusta.
                cam.previousProjection     = cam.projection;
                cam.previousViewProjection = cam.previousProjection * cam.previousView;
            }
            return cam;
        }

        [[nodiscard]] glm::vec3 lightDirectionFromTransformNormal(const TransformComponent& transform)
        {
            return safeNormalizeDirection(-glm::vec3(transform.worldMatrix[2]), glm::vec3 {0.0f, -1.0f, 0.0f});
        }

        [[nodiscard]] glm::mat4 areaLightSurfaceMatrix(const RenderLight& light)
        {
            const glm::vec3 normal = safeNormalizeDirection(light.direction, glm::vec3 {0.0f, -1.0f, 0.0f});
            glm::vec3       up {0.0f, 1.0f, 0.0f};
            if (std::abs(glm::dot(up, normal)) > 0.95f)
                up = glm::vec3 {1.0f, 0.0f, 0.0f};

            const glm::vec3 tangent =
                safeNormalizeDirection(glm::cross(up, normal), glm::vec3 {1.0f, 0.0f, 0.0f});
            const glm::vec3 bitangent =
                safeNormalizeDirection(glm::cross(normal, tangent), glm::vec3 {0.0f, 1.0f, 0.0f});

            glm::mat4 out {1.0f};
            out[0] = glm::vec4(tangent * std::max(light.width, 0.001f), 0.0f);
            out[1] = glm::vec4(-normal, 0.0f);
            out[2] = glm::vec4(bitangent * std::max(light.height, 0.001f), 0.0f);
            out[3] = glm::vec4(light.position, 1.0f);
            return out;
        }

        float effectiveGaussianAutomaticClodLevel(const GaussianSplatRenderSettings& settings)
        {
            if (!settings.foveatedClodActive())
                return std::clamp(settings.clodLevel, 0.01f, 1.0f);

            const glm::vec3 levels {
                std::clamp(settings.foveatedRingLevels.x, 0.0f, 1.0f),
                std::clamp(settings.foveatedRingLevels.y, 0.0f, 1.0f),
                std::clamp(settings.foveatedRingLevels.z, 0.0f, 1.0f),
            };
            return std::clamp(std::max(levels.x, std::max(levels.y, levels.z)), 0.01f, 1.0f);
        }

        uint32_t effectiveGaussianLodBudget(const GaussianSplatRenderSettings& settings, const uint32_t totalSplatCount)
        {
            // Baseline consumes the full table. Ordered CLOD consumes a prefix of
            // the table that vasset already sorted by importance at import time.
            if (!settings.lodBudgetEnabled())
                return totalSplatCount;

            // An explicit budget is useful for repeatable profiling. With budget 0
            // the UI exposes clodLevel as the paper-style continuous LOD fraction.
            if (settings.lodBudget > 0u)
                return std::min(totalSplatCount, settings.lodBudget);
            const float clodLevel = effectiveGaussianAutomaticClodLevel(settings);
            return std::min(
                totalSplatCount,
                std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<float>(totalSplatCount) * clodLevel))));
        }

        void applyGaussianSplatFoveatedClodSettings(resource::GpuSceneView&            gpuSceneView,
                                                    const GaussianSplatRenderSettings& settings)
        {
            const auto layers = settings.foveatedLayers();
            gpuSceneView.setGeneralGaussianSplatFoveatedClod(
                settings.foveatedClodActive(),
                settings.foveatedLayeredCompositeActive(),
                settings.foveatedGaze,
                glm::vec2 {layers[0].eccentricityDegrees, layers[1].eccentricityDegrees},
                glm::vec3 {layers[0].lodLevel, layers[1].lodLevel, layers[2].lodLevel},
                glm::vec3 {layers[0].resolutionScale, layers[1].resolutionScale, layers[2].resolutionScale},
                std::max(settings.foveatedTransitionDegrees, 0.0f));
        }


        bool gaussianSplatSelectionSettingsDirty(const GaussianSplatRenderSettings& current,
                                                 const GaussianSplatRenderSettings& applied)
        {
            return current.lodBudget != applied.lodBudget || current.clodLevel != applied.clodLevel ||
                   current.foveatedClodEnabled != applied.foveatedClodEnabled ||
                   current.foveatedRenderMode != applied.foveatedRenderMode ||
                   current.foveatedGaze != applied.foveatedGaze ||
                   current.foveatedRingDegrees != applied.foveatedRingDegrees ||
                   current.foveatedRingLevels != applied.foveatedRingLevels ||
                   current.foveatedResolutionScales != applied.foveatedResolutionScales ||
                   current.foveatedTransitionDegrees != applied.foveatedTransitionDegrees;
        }

        void updateGaussianSplatFoveatedBudgetController(GaussianSplatRenderSettings& settings, const double gpuFrameMs)
        {
            if (!settings.foveatedClodActive() || !settings.foveatedBudgetControllerEnabled || gpuFrameMs <= 0.0)
                return;

            const float targetMs = std::max(settings.foveatedTargetFrameMs, 0.1f);
            const float maxStep  = std::clamp(settings.foveatedBudgetAdjustRate, 0.001f, 0.25f);
            const float error    = static_cast<float>((targetMs - gpuFrameMs) / targetMs);
            if (std::abs(error) < 0.03f)
                return;

            const float signedStep = std::clamp(error * 0.5f, -maxStep, maxStep);
            auto        adjust     = [signedStep](float value, const float floorValue) {
                return std::clamp(value + signedStep * std::max(value, 0.1f), floorValue, 1.0f);
            };

            settings.foveatedRingLevels.z = adjust(settings.foveatedRingLevels.z, 0.01f);
            settings.foveatedRingLevels.y = adjust(settings.foveatedRingLevels.y, settings.foveatedRingLevels.z);
            if (error < -0.35f)
                settings.foveatedRingLevels.x = adjust(settings.foveatedRingLevels.x, settings.foveatedRingLevels.y);
            else
                settings.foveatedRingLevels.x = std::max(settings.foveatedRingLevels.x, settings.foveatedRingLevels.y);
        }

        void
        rebuildGaussianSplatOrderedClodPrefixSources(resource::GpuSceneView&                         gpuSceneView,
                                                     const std::vector<RenderGaussianSplatInstance>& gaussianSplats,
                                                     const resource::GpuResourcePool&                pool,
                                                     RuntimeProfiler&                                profiler)
        {
            gpuSceneView.generalGaussianSplatSelectedSources.clear();

            struct OrderedSource
            {
                uint32_t                                        rankNumerator {0};
                uint32_t                                        pointCount {1};
                uint32_t                                        drawIndex {0};
                uint32_t                                        rank {0};
                resource::GpuGeneralGaussianSplatSelectedSource selection {};
            };

            RuntimeProfiler::Scope     scope {profiler, "GaussianCLOD::BuildPrefix"};
            const bool                 singleDraw = gpuSceneView.generalGaussianSplatDraws.size() <= 1u;
            std::vector<OrderedSource> orderedSources;
            if (!singleDraw)
            {
                uint32_t reserveCount = 0u;
                for (const auto& splatInst : gaussianSplats)
                {
                    if (splatInst.splatIndex < pool.gaussianSplats.size())
                        reserveCount += pool.gaussianSplats[splatInst.splatIndex].pointCount;
                }
                orderedSources.reserve(reserveCount);
            }

            uint32_t drawIndex = 0u;
            for (const auto& splatInst : gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;
                if (drawIndex >= gpuSceneView.generalGaussianSplatDraws.size())
                    break;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                const auto&    drawRecord   = gpuSceneView.generalGaussianSplatDraws[drawIndex];
                const uint32_t sourceOffset = drawRecord.pointOffset;
                const uint32_t rankCount    = gpuSplat.pointCount;

                for (uint32_t rank = 0u; rank < rankCount; ++rank)
                {
                    const uint32_t localPoint = rank;

                    resource::GpuGeneralGaussianSplatSelectedSource selection {};
                    selection.sourceIndex  = sourceOffset + localPoint;
                    selection.drawIndex    = drawIndex;
                    selection.packedWeight = std::bit_cast<uint32_t>(1.0f);
                    selection.flags        = 0u;

                    if (singleDraw)
                    {
                        gpuSceneView.pushGeneralGaussianSplatSelectedSource(selection);
                    }
                    else
                    {
                        orderedSources.push_back(OrderedSource {
                            .rankNumerator = rank + 1u,
                            .pointCount    = gpuSplat.pointCount,
                            .drawIndex     = drawIndex,
                            .rank          = rank,
                            .selection     = selection,
                        });
                    }
                }

                ++drawIndex;
            }

            if (!singleDraw)
            {
                std::stable_sort(orderedSources.begin(), orderedSources.end(), [](const auto& a, const auto& b) {
                    const uint64_t lhs = static_cast<uint64_t>(a.rankNumerator) * static_cast<uint64_t>(b.pointCount);
                    const uint64_t rhs = static_cast<uint64_t>(b.rankNumerator) * static_cast<uint64_t>(a.pointCount);
                    if (lhs != rhs)
                        return lhs < rhs;
                    if (a.drawIndex != b.drawIndex)
                        return a.drawIndex < b.drawIndex;
                    return a.rank < b.rank;
                });

                gpuSceneView.generalGaussianSplatSelectedSources.reserve(orderedSources.size());
                for (const auto& source : orderedSources)
                    gpuSceneView.pushGeneralGaussianSplatSelectedSource(source.selection);
            }
        }

        void rebuildGaussianSplatSelectedSources(resource::GpuSceneView&                         gpuSceneView,
                                                 const std::vector<RenderGaussianSplatInstance>& gaussianSplats,
                                                 const resource::GpuResourcePool&                pool,
                                                 GaussianSplatFrameStats&                        stats,
                                                 RuntimeProfiler&                                profiler)
        {
            gpuSceneView.generalGaussianSplatSelectedSources.clear();

            // Baseline still builds a selected-source table so the preprocess
            // shader can share one path with Ordered CLOD.
            RuntimeProfiler::Scope scope {profiler, "GaussianSplat::BuildRawSelection"};
            uint32_t               drawIndex = 0u;
            for (const auto& splatInst : gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;
                if (drawIndex >= gpuSceneView.generalGaussianSplatDraws.size())
                    break;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                const auto&    drawRecord   = gpuSceneView.generalGaussianSplatDraws[drawIndex];
                const uint32_t sourceOffset = drawRecord.pointOffset;
                for (uint32_t localPoint = 0u; localPoint < gpuSplat.pointCount; ++localPoint)
                {
                    resource::GpuGeneralGaussianSplatSelectedSource selection {};
                    selection.sourceIndex  = sourceOffset + localPoint;
                    selection.drawIndex    = drawIndex;
                    selection.packedWeight = std::bit_cast<uint32_t>(1.0f);
                    selection.flags        = 0u;
                    gpuSceneView.pushGeneralGaussianSplatSelectedSource(selection);
                    ++stats.lodSelectedRawSplats;
                }

                ++drawIndex;
            }
        }
    } // namespace

    void RenderWorldCooker::cook(World&                 world,
                                 IAssetService&         assets,
                                 IGpuResourceService&   gpuResources,
                                 rhi::RenderDevice&     rd,
                                 GeometryFactory&       geometryFactory,
                                 RenderWorld&           out,
                                 IShaderService*        shaderService,
                                 const float            timeSeconds,
                                 rendering::GlyphAtlas* glyphAtlas)
    {
        RuntimeProfiler::ExternalScope cookScope {"RenderWorldCooker::cook/total"};
        out.clear();

        auto& reg = world.registry();

        {
            RuntimeProfiler::ExternalScope meshScope {"RenderWorldCooker::cook/meshes"};
            auto view = reg.view<IDComponent, TransformComponent, MeshComponent>();
            out.instances.reserve(view.size_hint());
            for (auto e : view)
            {
                const auto& id   = view.get<IDComponent>(e);
                const auto& tr   = view.get<TransformComponent>(e);
                const auto& mesh = view.get<MeshComponent>(e);
                if (!isEntityRenderable(world, reg, e))
                    continue;

                uint32_t meshIndex = std::numeric_limits<uint32_t>::max();
                if (mesh.builtinGeometry != UINT32_MAX)
                {
                    meshIndex = geometryFactory.getOrCreateMeshIndex(
                        static_cast<BuiltinGeometryKind>(mesh.builtinGeometry), gpuResources, rd);
                }
                else
                {
                    auto h = assets.loadMeshAsync(mesh.mesh);
                    if (!h.ready())
                        continue;
                    meshIndex = h.gpuIndex();
                }

                if (meshIndex == std::numeric_limits<uint32_t>::max())
                    continue;

                RenderInstance inst {};
                inst.entity      = id.uuid;
                inst.meshIndex   = meshIndex;
                inst.worldMatrix = tr.worldMatrix;
                inst.layerMask   = entityLayerMask(reg, e, kRenderLayerDefaultMask);
                const auto& pool = gpuResources.pool();
                if (meshIndex < pool.meshes.size())
                {
                    const auto& gpuMesh = pool.meshes[meshIndex];
                    if (const auto* palette = findSkinPaletteForMesh(reg, e, gpuMesh))
                    {
                        const size_t count = std::min(palette->matrices.size(), gpuMesh.inverseBindPoses.size());
                        inst.skinMatrices.resize(count);
                        for (size_t i = 0; i < count; ++i)
                            inst.skinMatrices[i] = palette->matrices[i] * gpuMesh.inverseBindPoses[i];
                        inst.skinMatrixCount = static_cast<uint32_t>(inst.skinMatrices.size());
                    }
                }
                inst.materialOverrides.reserve(mesh.materialOverrides.size());
                if (!mesh.materialOverrides.empty())
                {
                    RuntimeProfiler::ExternalScope materialScope {"RenderWorldCooker::cook/materialOverrides"};
                    for (const auto& materialOverride : mesh.materialOverrides)
                    {
                        uint32_t materialIndex = std::numeric_limits<uint32_t>::max();
                        if (!materialOverride.material.empty())
                        {
                            nlohmann::json propertyBlock;
                            std::string    propertyBlockText;
                            const auto*    propertyBlockPtr = static_cast<const nlohmann::json*>(nullptr);
                            if (!materialOverride.properties.empty())
                            {
                                propertyBlock = materialPropertyBlockToJson(materialOverride.properties);
                                if (!propertyBlock.empty())
                                {
                                    propertyBlockText = propertyBlock.dump();
                                    propertyBlockPtr  = &propertyBlock;
                                }
                            }
                            const auto materialKey =
                                propertyBlockPtr == nullptr ?
                                    std::string(materialOverride.material) :
                                    std::string(materialOverride.material) + "#slot" +
                                        std::to_string(materialOverride.slot) + "#" + propertyBlockText;
                            materialIndex = ensureMaterialAssetGpuMaterial(
                                assets,
                                shaderService,
                                gpuResources,
                                rd,
                                materialOverride.material,
                                timeSeconds,
                                propertyBlockPtr,
                                materialKey);
                        }
                        if (materialIndex == std::numeric_limits<uint32_t>::max() &&
                            !materialOverride.materialGraph.empty())
                        {
                            nlohmann::json propertyBlock;
                            std::string    propertyBlockText;
                            const auto*    propertyBlockPtr = static_cast<const nlohmann::json*>(nullptr);
                            if (!materialOverride.properties.empty())
                            {
                                propertyBlock = materialPropertyBlockToJson(materialOverride.properties);
                                if (!propertyBlock.empty())
                                {
                                    propertyBlockText = propertyBlock.dump();
                                    propertyBlockPtr  = &propertyBlock;
                                }
                            }
                            const auto materialKey =
                                propertyBlockPtr == nullptr ?
                                    std::string(materialOverride.materialGraph) :
                                    std::string(materialOverride.materialGraph) + "#slot" +
                                        std::to_string(materialOverride.slot) + "#" + propertyBlockText;
                            const auto ensureGraphConstant = [&]() {
                                return ensureMaterialGraphGpuMaterial(assets,
                                                                      gpuResources,
                                                                      rd,
                                                                      materialOverride.materialGraph,
                                                                      timeSeconds,
                                                                      propertyBlockPtr,
                                                                      materialKey);
                            };

                            // Prefer the graph's compiled per-pixel GLSL on first resolve. Once a graph has fallen
                            // back to a constant material, go straight to that path so its own time/content cache can
                            // handle updates without repeated shader variant probes.
                            if (hasGraphConstantGpuMaterial(gpuResources, materialKey))
                                materialIndex = ensureGraphConstant();
                            if (materialIndex == std::numeric_limits<uint32_t>::max())
                                materialIndex = ensureMaterialGraphShaderMaterial(
                                    assets,
                                    shaderService,
                                    gpuResources,
                                    rd,
                                    materialOverride.materialGraph,
                                    propertyBlockPtr,
                                    materialKey);
                            if (materialIndex == std::numeric_limits<uint32_t>::max())
                                materialIndex = ensureGraphConstant();
                        }
                        if (materialIndex != std::numeric_limits<uint32_t>::max())
                        {
                            inst.materialOverrides.push_back(RenderInstance::MaterialOverride {
                                .slot          = materialOverride.slot,
                                .materialIndex = materialIndex,
                            });
                        }
                    }
                }
                if (mesh.builtinGeometry != UINT32_MAX && mesh.materialOverrides.empty())
                {
                    const uint32_t materialIndex =
                        ensureBuiltinMaterialAssetGpuMaterial(assets, gpuResources, rd, kBuiltinDefaultMaterialUri);
                    if (materialIndex != std::numeric_limits<uint32_t>::max())
                    {
                        inst.materialOverrides.push_back(RenderInstance::MaterialOverride {
                            .slot          = 0u,
                            .materialIndex = materialIndex,
                        });
                    }
                }
                out.instances.push_back(inst);

                if (meshIndex < pool.meshes.size())
                {
                    const auto& gpuMesh = pool.meshes[meshIndex];
                    if (gpuMesh.meshletCount > 0 &&
                        gpuMesh.meshletOffset + gpuMesh.meshletCount <= pool.meshlets.cpuMeshlets.size())
                    {
                        for (uint32_t i = 0; i < gpuMesh.meshletCount; ++i)
                        {
                            const auto& meshlet = pool.meshlets.cpuMeshlets[gpuMesh.meshletOffset + i];
                            expandBounds(out, tr.worldMatrix, meshlet.center, meshlet.radius);
                        }
                    }
                }
            }
        }

        {
            RuntimeProfiler::ExternalScope splatScope {"RenderWorldCooker::cook/gaussianSplats"};
            auto splatView = reg.view<IDComponent, TransformComponent, GaussianSplatComponent>();
            out.gaussianSplats.reserve(splatView.size_hint());
            for (auto e : splatView)
            {
                const auto& id    = splatView.get<IDComponent>(e);
                const auto& tr    = splatView.get<TransformComponent>(e);
                const auto& splat = splatView.get<GaussianSplatComponent>(e);
                if (!isEntityRenderable(world, reg, e))
                    continue;

                auto h = assets.loadGaussianSplatAsync(splat.gaussianSplat);
                if (!h.ready())
                    continue;

                RenderGaussianSplatInstance inst {};
                inst.entity      = id.uuid;
                inst.splatIndex  = h.gpuIndex();
                inst.worldMatrix = tr.worldMatrix;
                inst.layerMask   = entityLayerMask(reg, e, kRenderLayerDefaultMask);
                out.gaussianSplats.push_back(inst);
            }
        }

        {
            RuntimeProfiler::ExternalScope emitterScope {"RenderWorldCooker::cook/particleEmitters"};
            auto emitterView = reg.view<IDComponent, TransformComponent, ParticleEmitterComponent>();
            out.emitters.reserve(emitterView.size_hint());
            for (auto e : emitterView)
            {
                const auto& ec = emitterView.get<ParticleEmitterComponent>(e);
                if (!ec.gpu)
                    continue; // CPU-backed emitters are previewed by ParticleSystem.
                if (!isEntityRenderable(world, reg, e))
                    continue;

                const auto& id = emitterView.get<IDComponent>(e);
                const auto& tr = emitterView.get<TransformComponent>(e);

                RenderParticleEmitter inst {};
                inst.entity    = id.uuid;
                inst.origin    = glm::vec3(tr.worldMatrix[3]);
                inst.emitter   = ec;
                inst.layerMask = entityLayerMask(reg, e, kRenderLayerDefaultMask);
                out.emitters.push_back(inst);
            }
        }

        {
            RuntimeProfiler::ExternalScope lightScope {"RenderWorldCooker::cook/lights"};
            auto lightView = reg.view<IDComponent, TransformComponent, LightComponent>();
            out.lights.reserve(lightView.size_hint());
            for (auto e : lightView)
            {
                const auto& id    = lightView.get<IDComponent>(e);
                const auto& tr    = lightView.get<TransformComponent>(e);
                const auto& light = lightView.get<LightComponent>(e);
                if (!isEntityRenderable(world, reg, e))
                    continue;

                RenderLight outLight {};
                outLight.entity           = id.uuid;
                outLight.kind             = static_cast<RenderLightKind>(light.kind);
                outLight.position         = glm::vec3(tr.worldMatrix[3]);
                outLight.direction        = lightDirectionFromTransformNormal(tr);
                outLight.color            = light.color;
                outLight.intensity        = light.intensity;
                outLight.range            = light.range;
                outLight.radius           = light.radius;
                outLight.width            = light.width;
                outLight.height           = light.height;
                outLight.innerConeDegrees = light.innerConeDegrees;
                outLight.outerConeDegrees = light.outerConeDegrees;
                outLight.castsShadow      = light.castsShadow;
                outLight.twoSided         = light.twoSided;
                out.lights.push_back(outLight);

                if (outLight.kind == RenderLightKind::eArea)
                {
                    const uint32_t meshIndex =
                        geometryFactory.getOrCreateMeshIndex(BuiltinGeometryKind::eQuad, gpuResources, rd);
                    if (meshIndex != std::numeric_limits<uint32_t>::max())
                    {
                        RenderInstance surface {};
                        surface.entity               = id.uuid;
                        surface.meshIndex            = meshIndex;
                        surface.worldMatrix          = areaLightSurfaceMatrix(outLight);
                        surface.baseColorOverride    = glm::vec4(outLight.color, 1.0f);
                        surface.hasBaseColorOverride = true;
                        surface.castsShadow          = false;
                        surface.layerMask            = entityLayerMask(reg, e, kRenderLayerDefaultMask);
                        out.instances.push_back(surface);

                        const auto& pool = gpuResources.pool();
                        if (meshIndex < pool.meshes.size())
                        {
                            const auto& gpuMesh = pool.meshes[meshIndex];
                            if (gpuMesh.meshletCount > 0 &&
                                gpuMesh.meshletOffset + gpuMesh.meshletCount <= pool.meshlets.cpuMeshlets.size())
                            {
                                for (uint32_t i = 0; i < gpuMesh.meshletCount; ++i)
                                {
                                    const auto& meshlet = pool.meshlets.cpuMeshlets[gpuMesh.meshletOffset + i];
                                    expandBounds(out, surface.worldMatrix, meshlet.center, meshlet.radius);
                                }
                            }
                        }
                    }
                }
            }
        }

        {
            RuntimeProfiler::ExternalScope envScope {"RenderWorldCooker::cook/environment"};
            auto environmentView = reg.view<EnvironmentComponent>();
            for (auto e : environmentView)
            {
                const auto& environment = environmentView.get<EnvironmentComponent>(e);
                if (!environment.active)
                    continue;
                if (!isEntityRenderable(world, reg, e))
                    continue;

                out.environment.active           = true;
                out.environment.ambientColor     = environment.ambientColor;
                out.environment.ambientIntensity = environment.ambientIntensity;
                out.environment.enableIBL        = environment.enableIBL;
                out.environment.iblColor         = environment.iblColor;
                out.environment.iblIntensity     = environment.iblIntensity;

                if (environment.skybox.valid())
                {
                    auto        skybox = assets.loadTextureAsync(environment.skybox);
                    const auto& pool   = gpuResources.pool();
                    if (skybox.ready() && skybox.gpuIndex() < pool.textures.size())
                        out.environment.skybox = pool.textures[skybox.gpuIndex()].texture.get();
                }
                break;
            }
        }

        {
            RuntimeProfiler::ExternalScope probeScope {"RenderWorldCooker::cook/reflectionProbes"};
            auto reflectionProbeView = reg.view<IDComponent, TransformComponent, ReflectionProbeComponent>();
            out.reflectionProbes.reserve(reflectionProbeView.size_hint());
            for (auto e : reflectionProbeView)
            {
                const auto& id    = reflectionProbeView.get<IDComponent>(e);
                const auto& tr    = reflectionProbeView.get<TransformComponent>(e);
                const auto& probe = reflectionProbeView.get<ReflectionProbeComponent>(e);
                if (!probe.active)
                    continue;
                if (!isEntityRenderable(world, reg, e))
                    continue;
                rhi::Texture* environmentMap = nullptr;
                if (probe.enableIBL)
                {
                    if (!probe.environmentMap.valid())
                        continue;
                    auto        map  = assets.loadTextureAsync(probe.environmentMap);
                    const auto& pool = gpuResources.pool();
                    if (!map.ready() || map.gpuIndex() >= pool.textures.size())
                        continue;
                    environmentMap = pool.textures[map.gpuIndex()].texture.get();
                }

                if (probe.enableIBL && !environmentMap)
                    continue;

                RenderReflectionProbe outProbe {};
                outProbe.entity             = id.uuid;
                outProbe.position           = glm::vec3(tr.worldMatrix[3]);
                outProbe.halfExtents        = glm::max(probe.boxSize * 0.5f, glm::vec3 {0.01f});
                outProbe.radius             = std::max(probe.radius, 0.01f);
                outProbe.blendDistance      = std::max(probe.blendDistance, 0.0f);
                outProbe.intensity          = std::max(probe.intensity, 0.0f);
                outProbe.priority           = probe.priority;
                outProbe.enableIBL          = probe.enableIBL;
                outProbe.parallaxCorrection = probe.parallaxCorrection;
                outProbe.shape = probe.shape == 1u ? RenderReflectionProbeShape::eSphere : RenderReflectionProbeShape::eBox;
                outProbe.environmentMap = environmentMap;
                out.reflectionProbes.push_back(outProbe);
            }
        }

        {
            RuntimeProfiler::ExternalScope uiScope {"RenderWorldCooker::cook/ui"};
            // Only touch the glyph atlas when the world actually has renderable text, so text-less
            // scenes never allocate the FreeType library or the atlas GPU texture.
            bool hasText = false;
            for (auto textEntity : reg.view<UiTextComponent>())
            {
                const auto& textC = reg.get<UiTextComponent>(textEntity);
                if (textC.enabled && !textC.text.empty())
                {
                    hasText = true;
                    break;
                }
            }

            uint32_t               glyphAtlasIndex = 0u;
            rendering::GlyphAtlas* uiGlyphAtlas    = (glyphAtlas && hasText) ? glyphAtlas : nullptr;
            if (uiGlyphAtlas)
            {
                glyphAtlasIndex       = uiGlyphAtlas->ensureRegistered(rd, gpuResources);
                out.glyphAtlasTexture = uiGlyphAtlas->texture();
            }
            cookUi(world, assets, out, uiGlyphAtlas, glyphAtlasIndex);
            if (uiGlyphAtlas)
                uiGlyphAtlas->flush(rd);
        }
    }

    bool RenderSystem::onInit()
    {
        VULTRA_CORE_INFO("[RenderSystem] Initializing...");

#if defined(__ANDROID__)
        // Android paths currently rely on the CPU-driven renderer.
        m_EnableGpuDrivenMeshletPipeline = false;
#endif

        VULTRA_CORE_TRACE("[RenderSystem] Getting render backend service");
        auto& backendService = ctx().services.require<IRenderBackendService>();

        VULTRA_CORE_TRACE("[RenderSystem] Getting window service");
        auto& windowService = ctx().services.require<IWindowService>();

        VULTRA_CORE_TRACE("[RenderSystem] Creating transient resources");
        m_TransientResources = createScope<framegraph::TransientResources>(backendService.renderDevice());

        VULTRA_CORE_TRACE("[RenderSystem] Providing IRenderService");
        ctx().services.provide<IRenderService>(this);

        if (!ctx().config.render.renderPipelineAsset.empty())
        {
            auto* assetService = ctx().services.tryGet<IAssetService>();
            auto  pipelineText = assetService ?
                                     assetService->loadTextAssetSync(ctx().config.render.renderPipelineAsset) :
                                     vbase::Result<std::string, std::string>::err("asset service unavailable");
            if (pipelineText)
            {
                VULTRA_CORE_INFO("[RenderSystem] Using render pipeline '{}'", ctx().config.render.renderPipelineAsset);
                const auto rendererKey = ctx().config.render.renderPipelineRendererKey.empty() ?
                                             rendererKeyFromRenderGraphUri(ctx().config.render.renderPipelineAsset) :
                                             ctx().config.render.renderPipelineRendererKey;
                m_Renderers[rendererKey] =
                    createRef<DeclarativeRenderer>(ctx().config.render.renderPipelineAsset, rendererKey);
                m_DefaultRendererKey = rendererKey;
            }
            else
            {
                VULTRA_CORE_WARN(
                    "[RenderSystem] Render pipeline '{}' is unavailable: {}. Falling back to registered renderer.",
                    ctx().config.render.renderPipelineAsset,
                    std::move(pipelineText).error());
            }
        }

        VULTRA_CORE_TRACE("[RenderSystem] Initializing renderers");
        for (auto& [key, renderer] : m_Renderers)
        {
            VULTRA_CORE_TRACE("[RenderSystem]     Initializing renderer: {}", key);
            Services services = ctx().services;
            renderer->setupServices(services);
            renderer->init();
        }
        m_Initialized = true;

        VULTRA_CORE_TRACE("[RenderSystem] Initializing samplers");
        m_Samplers["default"] = backendService.renderDevice().getSampler(rhi::SamplerInfo {});
        m_Samplers["linear"]  = backendService.renderDevice().getSampler(
            rhi::SamplerInfo {.magFilter = rhi::TexelFilter::eLinear, .minFilter = rhi::TexelFilter::eLinear});
        m_Samplers["bilinear"] = m_Samplers["linear"];
        m_Samplers["nearest"]  = backendService.renderDevice().getSampler(
            rhi::SamplerInfo {.magFilter = rhi::TexelFilter::eNearest, .minFilter = rhi::TexelFilter::eNearest});

        const auto initialExtent = backendService.swapchain().getExtent();
        if (initialExtent.width > 0u && initialExtent.height > 0u)
            onResize(initialExtent.width, initialExtent.height);

        VULTRA_CORE_TRACE("[RenderSystem] Initializing debug draw");
        commonContext.debugDraw = createRef<DebugDrawInterface>();
        commonContext.debugDraw->initialize(backendService.renderDevice(),
                                            backendService.swapchain().getPixelFormat());
        dd::initialize(commonContext.debugDraw.get());

        VULTRA_CORE_INFO("[RenderSystem] Initialized!");

        return true;
    }

    void RenderSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[RenderSystem] Shutting down");

        auto& backendService = ctx().services.require<IRenderBackendService>();
        backendService.renderDevice().waitIdle();

        dd::shutdown();
        commonContext.debugDraw.reset();

        m_GpuSceneViewBack.clear();
        m_GpuSceneViewFront.clear();
        m_GpuSceneDatabaseBack.clear();
        m_GpuSceneDatabaseFront.clear();
        m_GpuSceneDirtyTracker.reset();

        // Release persistent GPU particle pools while the device is still alive (after waitIdle).
        m_ParticleManager.clear();

        // Release the glyph atlas GPU texture while the device is still alive (its Ref otherwise
        // outlives the device until ~RenderSystem, leaking the image/VMA allocation).
        m_GlyphAtlas.releaseGpu();

        for (auto& [key, renderer] : m_Renderers)
            renderer = nullptr;
        m_Renderers.clear();

        m_FrameGraphTextureCaptureEnabled = false;
        clearFrameGraphDebugState();

        m_TransientResources.reset();

        m_FrameResources.clear();
        m_Initialized = false;
    }

    void RenderSystem::registerRenderer(Ref<Renderer> renderer)
    {
        if (!renderer)
            return;
        if (m_Renderers.contains(std::string(renderer->name())))
            return;
        if (m_Initialized)
        {
            Services services = ctx().services;
            renderer->setupServices(services);
            renderer->init();
        }
        m_Renderers[std::string(renderer->name())] = renderer;
    }

    std::vector<std::string> RenderSystem::rendererKeys() const
    {
        std::vector<std::string> keys;
        keys.reserve(m_Renderers.size());
        for (const auto& [key, renderer] : m_Renderers)
        {
            if (renderer)
                keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    Ref<Renderer> RenderSystem::resolveRenderer(const RenderCamera& cam) const
    {
        if (rendererRequiresRayTracingScene(cam.rendererKey))
        {
            const auto* renderBackend = ctx().services.tryGet<IRenderBackendService>();
            const bool  rayTracingAvailable =
                renderBackend &&
                HasFlagValues(const_cast<IRenderBackendService*>(renderBackend)->renderDevice().getFeatureFlag(),
                              rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline);
            if (!rayTracingAvailable)
            {
                if (auto fallback = m_Renderers.find("universal"); fallback != m_Renderers.end())
                    return fallback->second;
            }
        }

        if (auto it = m_Renderers.find(cam.rendererKey); it != m_Renderers.end())
            return it->second;

        if (auto it2 = m_Renderers.find(m_DefaultRendererKey); it2 != m_Renderers.end())
            return it2->second;

        return nullptr;
    }

    bool RenderSystem::rendererRequiresRayTracingScene(std::string_view rendererKey) const
    {
        if (rendererKeyRequiresRayTracingScene(rendererKey))
            return true;

        if (auto it = m_Renderers.find(std::string(rendererKey)); it != m_Renderers.end())
            return it->second && it->second->requiresRayTracingScene();

        return false;
    }

    void RenderSystem::onResize(uint32_t width, uint32_t height)
    {
        for (auto& [key, renderer] : m_Renderers)
        {
            if (renderer)
                renderer->onResize(width, height);
        }
    }

    void RenderSystem::resetSceneState()
    {
        m_RenderWorldFront.clear();
        m_RenderWorldBack.clear();
        m_GpuSceneViewBack.clear();
        m_GpuSceneViewFront.clear();
        m_GpuSceneDatabaseBack.clear();
        m_GpuSceneDatabaseFront.clear();
        m_GpuSceneDirtyTracker.reset();
        m_OverrideRenderWorlds.clear();
        m_GeometryFactory.clear();
        m_ParticleManager.clear();
    }

    bool RenderSystem::reloadRenderPipeline()
    {
        if (m_InRenderFrame)
        {
            m_PendingRenderPipelineReload = true;
            m_PendingRenderPipelineAsset.clear();
            m_PendingRenderPipelineRendererKey.clear();
            VULTRA_CORE_INFO("[RenderSystem] Queued render pipeline reload for next frame");
            return true;
        }

        return reloadRenderPipelineNow();
    }

    bool RenderSystem::reloadRenderPipeline(std::string_view asset, std::string_view rendererKey)
    {
        if (asset.empty())
            return false;

        if (m_InRenderFrame)
        {
            m_PendingRenderPipelineReload = true;
            m_PendingRenderPipelineAsset  = std::string {asset};
            m_PendingRenderPipelineRendererKey =
                rendererKey.empty() ? rendererKeyFromRenderGraphUri(asset) : std::string {rendererKey};
            VULTRA_CORE_INFO("[RenderSystem] Queued render pipeline reload for next frame");
            return true;
        }

        return reloadRenderPipelineNow(asset, rendererKey);
    }

    bool RenderSystem::updateRenderGraph(std::string_view asset, std::string_view rendererKey)
    {
        if (asset.empty())
            return false;

        if (m_InRenderFrame)
        {
            m_PendingRenderGraphUpdate            = true;
            m_PendingRenderGraphUpdateAsset       = std::string {asset};
            m_PendingRenderGraphUpdateRendererKey = rendererKey.empty() ? rendererKeyFromRenderGraphUri(asset) :
                                                                          std::string {rendererKey};
            return true;
        }

        return updateRenderGraphNow(asset, rendererKey);
    }

    bool RenderSystem::reloadRenderPipelineNow()
    {
        const auto& renderConfig = ctx().config.render;
        if (renderConfig.renderPipelineAsset.empty())
            return false;

        if (auto* backendService = ctx().services.tryGet<IRenderBackendService>())
            backendService->renderDevice().waitIdle();
        clearFrameGraphDebugState();

        const auto rendererKey = renderConfig.renderPipelineRendererKey.empty() ?
                                     rendererKeyFromRenderGraphUri(renderConfig.renderPipelineAsset) :
                                     renderConfig.renderPipelineRendererKey;

        auto     renderer = createRef<DeclarativeRenderer>(renderConfig.renderPipelineAsset, rendererKey);
        Services services = ctx().services;
        renderer->setupServices(services);
        renderer->init();
        m_Renderers[rendererKey]      = std::move(renderer);
        m_DefaultRendererKey          = rendererKey;
        m_PendingRenderPipelineReload = false;
        VULTRA_CORE_INFO("[RenderSystem] Reloaded render pipeline '{}'", renderConfig.renderPipelineAsset);
        return true;
    }

    bool RenderSystem::reloadRenderPipelineNow(std::string_view asset, std::string_view rendererKey)
    {
        if (asset.empty())
            return false;

        if (auto* backendService = ctx().services.tryGet<IRenderBackendService>())
            backendService->renderDevice().waitIdle();
        clearFrameGraphDebugState();

        auto     key      = rendererKey.empty() ? rendererKeyFromRenderGraphUri(asset) : std::string {rendererKey};
        auto     renderer = createRef<DeclarativeRenderer>(std::string {asset}, key);
        Services services = ctx().services;
        renderer->setupServices(services);
        renderer->init();
        m_Renderers[key]              = std::move(renderer);
        m_PendingRenderPipelineReload = false;
        m_PendingRenderPipelineAsset.clear();
        m_PendingRenderPipelineRendererKey.clear();
        VULTRA_CORE_INFO("[RenderSystem] Reloaded render pipeline '{}'", asset);
        return true;
    }

    bool RenderSystem::updateRenderGraphNow(std::string_view asset, std::string_view rendererKey)
    {
        if (asset.empty())
            return false;

        auto key = rendererKey.empty() ? rendererKeyFromRenderGraphUri(asset) : std::string {rendererKey};
        auto it  = m_Renderers.find(key);
        if (it == m_Renderers.end() || !it->second)
            return false;

        auto renderer = std::dynamic_pointer_cast<DeclarativeRenderer>(it->second);
        if (!renderer)
            return false;

        const bool updated = renderer->updateRenderGraph(asset);
        if (updated)
        {
            m_PendingRenderGraphUpdate = false;
            m_PendingRenderGraphUpdateAsset.clear();
            m_PendingRenderGraphUpdateRendererKey.clear();
        }
        return updated;
    }

    bool RenderSystem::reloadProjectShaderLibrary(std::string_view uri)
    {
        if (uri.empty())
            return false;

        auto* shaderService = ctx().services.tryGet<IShaderService>();
        if (!shaderService || !shaderService->reloadProjectLibrary(uri))
            return false;

        for (auto& [_, renderer] : m_Renderers)
        {
            auto declarative = std::dynamic_pointer_cast<DeclarativeRenderer>(renderer);
            if (declarative)
                declarative->invalidateShaderPipelines();
        }
        return true;
    }

    void RenderSystem::clearFrameGraphDebugState()
    {
        m_FrameGraphTexturePreviewPipeline.reset();
        m_FrameGraphTexturePreviewPipelineFormat = rhi::PixelFormat::eUndefined;
        m_FrameGraphDebugTextures.clear();
        m_FrameGraphDebugTextureSlots.clear();
        m_RetiredFrameGraphDebugTextureSlots.clear();
        m_FrameGraphTexturePreviewOverrides.clear();
        m_FrameGraphTexturePreviewSettings = {};
        m_LastFrameGraphSnapshot.clear();
    }

    void RenderSystem::releaseOverrideRenderWorld(World* world)
    {
        if (!world)
            return;

        for (auto& slot : m_OverrideRenderWorlds)
        {
            if (slot.world != world)
                continue;
            slot.world            = nullptr;
            slot.lastTouchedFrame = m_FrameCounter;
        }
    }

    rhi::GraphicsPipeline* RenderSystem::getFrameGraphTexturePreviewPipeline(rhi::RenderDevice&         rd,
                                                                             rhi::ShaderLibraryRuntime& shaderLib,
                                                                             const rhi::PixelFormat     colorFormat)
    {
        if (m_FrameGraphTexturePreviewPipeline && m_FrameGraphTexturePreviewPipelineFormat == colorFormat)
        {
            return &*m_FrameGraphTexturePreviewPipeline;
        }

        const auto vertexHash = rhi::ShaderLibraryRuntime::computeVariantHash(
            "builtin/general/fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert, {});
        const auto fragmentHash = rhi::ShaderLibraryRuntime::computeVariantHash(
            "builtin/general/frame_debugger_texture_preview.frag", vshadersystem::ShaderStage::eFrag, {});
        auto vertexShader   = shaderLib.load(vertexHash, vshadersystem::ShaderStage::eVert);
        auto fragmentShader = shaderLib.load(fragmentHash, vshadersystem::ShaderStage::eFrag);
        if (!vertexShader || !fragmentShader)
        {
            VULTRA_CORE_ERROR("[FrameDebugger] Failed to load texture preview shaders");
            m_FrameGraphTexturePreviewPipeline.reset();
            m_FrameGraphTexturePreviewPipelineFormat = rhi::PixelFormat::eUndefined;
            return nullptr;
        }

        m_FrameGraphTexturePreviewPipeline = rhi::GraphicsPipeline::Builder {}
                                                 .setColorFormats({colorFormat})
                                                 .setInputAssembly({})
                                                 .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
                                                 .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
                                                 .setDepthStencil({
                                                     .depthTest  = false,
                                                     .depthWrite = false,
                                                 })
                                                 .setRasterizer({
                                                     .polygonMode = rhi::PolygonMode::eFill,
                                                     .cullMode    = rhi::CullMode::eNone,
                                                 })
                                                 .setBlending(0, {.enabled = false})
                                                 .build(rd);
        m_FrameGraphTexturePreviewPipelineFormat = colorFormat;
        return m_FrameGraphTexturePreviewPipeline ? &*m_FrameGraphTexturePreviewPipeline : nullptr;
    }

    void RenderSystem::addFrameGraphTextureCapturePasses(FrameGraphBuildContext& ctx, const RenderCamera& camera)
    {
        const bool dumpCaptureActive = m_FrameGraphTextureDumpCaptureFrames > 0;
        if (!m_FrameGraphTextureCaptureEnabled && !dumpCaptureActive)
            return;
        if (ctx.rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
            return;
        auto lowerAscii = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
        };
        const auto dumpFilter   = lowerAscii(m_FrameGraphTextureDumpFilter);
        const bool dumpCameraOk = !dumpCaptureActive || m_FrameGraphTextureDumpCamera.empty() ||
                                  camera.name == m_FrameGraphTextureDumpCamera;
        const bool dumpRendererOk = !dumpCaptureActive || m_FrameGraphTextureDumpRenderer.empty() ||
                                    camera.rendererKey == m_FrameGraphTextureDumpRenderer;
        if (dumpCaptureActive && (!dumpCameraOk || !dumpRendererOk))
            return;

        struct CaptureCandidate
        {
            FrameGraphResource resource {};
            uint32_t           resourceNodeId {0};
            uint32_t           resourceVersion {0};
            std::string        name;
            rhi::ImageAspect   aspect {rhi::ImageAspect::eColor};
            uint32_t           layer {0};
            uint32_t           layerCount {1};
            bool               imported {false};
            bool               capturable {false};
        };

        struct PassData
        {
            FrameGraphResource source;
            FrameGraphResource target;
        };

        struct alignas(16) PreviewPushConstants
        {
            glm::ivec4 channelMask {1, 1, 1, 1};
            int        gammaCorrect {0};
            int        previewMode {0};
            float      depthNear {0.1f};
            float      depthFar {1000.0f};
            float      clampMin {0.0f};
            float      clampMax {1.0f};
        };

        struct TextureResourceCollector
        {
            std::vector<CaptureCandidate> candidates;

            static rhi::ImageAspect imageAspectFor(rhi::PixelFormat format)
            {
                const auto aspectMask = rhi::getAspectMask(format);
                if (HasFlagValues(aspectMask, rhi::ImageAspectFlags::eDepth))
                    return rhi::ImageAspect::eDepth;
                return rhi::ImageAspect::eColor;
            }

            static bool canPreviewWithFloatSampler(rhi::PixelFormat format)
            {
                switch (format)
                {
                    using enum rhi::PixelFormat;

                    case eR8UI:
                    case eR8I:
                    case eRG8UI:
                    case eRG8I:
                    case eRGBA8UI:
                    case eRGBA8I:
                    case eR16UI:
                    case eR16I:
                    case eRG16UI:
                    case eRG16I:
                    case eRGBA16UI:
                    case eRGBA16I:
                    case eR32UI:
                    case eR32I:
                    case eRG32UI:
                    case eRG32I:
                    case eRGBA32UI:
                    case eRGBA32I:
                    case eStencil8:
                        return false;
                    default:
                        return format != eUndefined;
                }
            }

            void operator()(const PassNode&, const std::vector<ResourceNode>&) {}

            void operator()(const ResourceNode& resource, const ResourceEntry& entry, const std::vector<PassNode>&)
            {
                // fg has no public runtime type tag. FrameGraphTexture::toString includes usage metadata;
                // buffer resources do not, so this keeps capture automatic without touching fg internals.
                if (entry.toString().find("<BR/>Usage = ") == std::string::npos)
                    return;

                const auto& desc = entry.getDescriptor<framegraph::FrameGraphTexture>();
                if (desc.format == rhi::PixelFormat::eUndefined || desc.extent.width == 0u || desc.extent.height == 0u)
                    return;

                std::string name(resource.getName());
                if (resource.getVersion() > ResourceEntry::kInitialVersion)
                    name += " v" + std::to_string(resource.getVersion());
                const auto layerCount = std::max(desc.layers, 1u);
                const bool capturable = canPreviewWithFloatSampler(desc.format) &&
                                        static_cast<bool>(desc.usageFlags & rhi::ImageUsage::eSampled);
                for (uint32_t layer = 0u; layer < layerCount; ++layer)
                {
                    auto layerName = name;
                    if (layerCount > 1u)
                        layerName += layer == 0u ? " [Left Eye]" :
                                     layer == 1u ? " [Right Eye]" :
                                                   " [Layer " + std::to_string(layer) + "]";
                    candidates.push_back(CaptureCandidate {
                        .resource        = static_cast<FrameGraphResource>(resource.getId()),
                        .resourceNodeId  = resource.getResourceId(),
                        .resourceVersion = resource.getVersion(),
                        .name            = std::move(layerName),
                        .aspect          = imageAspectFor(desc.format),
                        .layer           = layer,
                        .layerCount      = layerCount,
                        .imported        = entry.isImported(),
                        .capturable      = capturable,
                    });
                }
            }

            void flush(std::ostream&) const {}
        };

        std::ostringstream       unused;
        TextureResourceCollector collector;
        ctx.fg.debugOutput(unused, collector);

        for (const auto& candidate : collector.candidates)
        {
            auto       source     = candidate.resource;
            const auto sourceDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(source);
            if (sourceDesc.format == rhi::PixelFormat::eUndefined || sourceDesc.extent.width == 0u ||
                sourceDesc.extent.height == 0u)
            {
                continue;
            }

            std::string cameraName {camera.name.empty() ? std::string {"Camera"} : camera.name};
            std::string slotKey = cameraName + "/" + candidate.name + "/layer:" + std::to_string(candidate.layer);
            if (dumpCaptureActive && !dumpFilter.empty())
            {
                const auto haystack = lowerAscii(cameraName + " " + camera.rendererKey + " " + candidate.name + " " +
                                                 slotKey + " R" + std::to_string(candidate.resourceNodeId) + "_" +
                                                 std::to_string(candidate.resourceVersion));
                if (haystack.find(dumpFilter) == std::string::npos)
                    continue;
            }
            std::string transientResourceKey = "R" + std::to_string(candidate.resourceNodeId) + "_" +
                                               std::to_string(candidate.resourceVersion) + "/layer:" +
                                               std::to_string(candidate.layer);
            const auto overrideIt          = m_FrameGraphTexturePreviewOverrides.find(slotKey);
            FrameGraphTexturePreviewSettings dumpPreviewSettings {};
            dumpPreviewSettings.selectedTextureKey = std::string(FrameGraphTexturePreviewSettings::kCaptureAllTextures);
            dumpPreviewSettings.maxPreviewExtent   = m_FrameGraphTextureDumpMaxPreviewExtent;
            const auto previewSettings = dumpCaptureActive ?
                                             dumpPreviewSettings :
                                         overrideIt != m_FrameGraphTexturePreviewOverrides.end() ?
                                             overrideIt->second :
                                             m_FrameGraphTexturePreviewSettings;
            const bool captureAllRequested = m_FrameGraphTexturePreviewSettings.selectedTextureKey ==
                                             FrameGraphTexturePreviewSettings::kCaptureAllTextures;
            const bool captureNoneRequested = m_FrameGraphTexturePreviewSettings.selectedTextureKey ==
                                              FrameGraphTexturePreviewSettings::kCaptureNoTextures;
            const bool shouldPreview = dumpCaptureActive ?
                                           true :
                                       captureAllRequested ?
                                           true :
                                       captureNoneRequested ?
                                           false :
                                       m_FrameGraphTexturePreviewSettings.selectedTextureKey.empty() ?
                                           m_FrameGraphDebugTextures.empty() :
                                           slotKey == m_FrameGraphTexturePreviewSettings.selectedTextureKey;
            auto previewExtent = sourceDesc.extent;
            if (previewSettings.maxPreviewExtent > 0u)
            {
                const uint32_t maxSourceExtent = std::max(sourceDesc.extent.width, sourceDesc.extent.height);
                if (maxSourceExtent > previewSettings.maxPreviewExtent)
                {
                    previewExtent.width = std::max(
                        1u,
                        static_cast<uint32_t>((static_cast<uint64_t>(sourceDesc.extent.width) *
                                               previewSettings.maxPreviewExtent) /
                                              maxSourceExtent));
                    previewExtent.height = std::max(
                        1u,
                        static_cast<uint32_t>((static_cast<uint64_t>(sourceDesc.extent.height) *
                                               previewSettings.maxPreviewExtent) /
                                              maxSourceExtent));
                }
            }
            std::string publicKey = slotKey + "@" + std::to_string(previewExtent.width) + "x" +
                                    std::to_string(previewExtent.height) + ":" +
                                    std::string(rhi::toString(sourceDesc.format));
            if (!candidate.capturable || !shouldPreview)
            {
                m_FrameGraphDebugTextures.push_back(FrameGraphDebugTexture {
                    .camera               = cameraName,
                    .renderer             = camera.rendererKey,
                    .name                 = candidate.name,
                    .key                  = publicKey,
                    .resourceKey          = slotKey,
                    .transientResourceKey = transientResourceKey,
                    .texture              = nullptr,
                    .layer                = candidate.layer,
                    .layerCount           = candidate.layerCount,
                    .imported             = candidate.imported,
                    .capturable           = candidate.capturable,
                    .extent               = previewExtent,
                    .sourceExtent         = sourceDesc.extent,
                    .format               = sourceDesc.format,
                    .zNear                = camera.zNear,
                    .zFar                 = camera.zFar,
                });
                continue;
            }

            auto&      slot     = m_FrameGraphDebugTextureSlots[publicKey];
            const bool recreate = !slot.texture || slot.extent.width != previewExtent.width ||
                                  slot.extent.height != previewExtent.height ||
                                  slot.format != rhi::PixelFormat::eRGBA8_UNorm;
            if (recreate)
            {
                if (slot.texture)
                {
                    slot.lastTouchedFrame = m_FrameCounter;
                    m_RetiredFrameGraphDebugTextureSlots.push_back(std::move(slot));
                    slot = {};
                }

                slot.texture = ctx.rd.createTexture2D(previewExtent,
                                                       rhi::PixelFormat::eRGBA8_UNorm,
                                                       1,
                                                       0,
                                                       rhi::ImageUsage::eSampled | rhi::ImageUsage::eRenderTarget |
                                                           rhi::ImageUsage::eTransferSrc);
            }

            slot.camera           = cameraName;
            slot.name             = candidate.name;
            slot.key              = publicKey;
            slot.extent           = previewExtent;
            slot.format           = rhi::PixelFormat::eRGBA8_UNorm;
            slot.lastTouchedFrame = m_FrameCounter;

            if (!slot.texture)
                continue;

            auto target = framegraph::importTexture(ctx.fg, std::string {"DebugCapture/"} + slotKey, &*slot.texture);
            ctx.fg.addCallbackPass<PassData>(
                std::string {"DebugCapture/"} + candidate.name,
                [source, target, aspect = candidate.aspect](FrameGraph::Builder& builder, PassData& pd) {
                    pd.source = builder.read(source,
                                             framegraph::TextureRead {
                                                 .binding =
                                                     {
                                                         .location      = {.set = 3, .binding = 0},
                                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                     },
                                                 .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                                 .imageAspect = aspect,
                                             });
                    pd.target = builder.write(target,
                                              framegraph::Attachment {
                                                  .index       = 0,
                                                  .imageAspect = rhi::ImageAspect::eColor,
                                                  .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                              });
                },
                [this, preview = previewSettings, sourceLayer = candidate.layer, sourceAspect = candidate.aspect](
                    const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                    VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                    if (!rc.ext.builtinShaderLib)
                        return;
                    assert(rc.framebufferInfo().has_value());
                    const auto fb       = rc.framebufferInfo().value();
                    auto*      pipeline = getFrameGraphTexturePreviewPipeline(
                        rc.rd,
                        *rc.ext.builtinShaderLibForProfile(rhi::ShaderProfile::eGeneral),
                        rhi::getColorFormat(fb, 0));
                    if (!pipeline)
                        return;

                    PreviewPushConstants pc {
                        .channelMask = glm::ivec4(preview.channels[0] ? 1 : 0,
                                                  preview.channels[1] ? 1 : 0,
                                                  preview.channels[2] ? 1 : 0,
                                                  preview.channels[3] ? 1 : 0),
                        .gammaCorrect = preview.gammaCorrect ? 1 : 0,
                        .previewMode  = preview.previewMode,
                        .depthNear    = preview.depthNear,
                        .depthFar     = preview.depthFar,
                        .clampMin     = preview.clampMin,
                        .clampMax     = preview.clampMax,
                    };

                    auto*      sourceTexture = resources.get<framegraph::FrameGraphTexture>(data.source).texture;
                    const auto samplerIt     = rc.ext.samplers.find("nearest");
                    if (sourceTexture && samplerIt != rc.ext.samplers.end())
                    {
                        rc.resourceSet[3][0] = rhi::bindings::CombinedImageSampler {
                            .texture     = sourceTexture,
                            .imageAspect = sourceAspect,
                            .sampler     = samplerIt->second,
                            .layer = sourceTexture->getNumLayers() > 1u ? std::optional {sourceLayer} : std::nullopt,
                        };
                    }
                    rc.cb.bindPipeline(*pipeline);
                    rc.bindDescriptorSets(*pipeline);
                    rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                    rc.cb.beginRendering(fb).drawFullScreenTriangle().endRendering();
                });

            m_FrameGraphDebugTextures.push_back(FrameGraphDebugTexture {
                .camera               = slot.camera,
                .renderer             = camera.rendererKey,
                .name                 = slot.name,
                .key                  = slot.key,
                .resourceKey          = slotKey,
                .transientResourceKey = transientResourceKey,
                .texture              = &*slot.texture,
                .layer                = candidate.layer,
                .layerCount           = candidate.layerCount,
                .imported             = candidate.imported,
                .capturable           = candidate.capturable,
                .extent               = slot.extent,
                .sourceExtent         = sourceDesc.extent,
                .format               = sourceDesc.format,
                .zNear                = camera.zNear,
                .zFar                 = camera.zFar,
            });
        }
    }

    void RenderSystem::renderFrame()
    {
        m_InRenderFrame = true;
        struct RenderFrameGuard
        {
            bool& value;
            ~RenderFrameGuard() { value = false; }
        } renderFrameGuard {m_InRenderFrame};

        const auto renderFrameCpuStart = std::chrono::steady_clock::now();

        auto& backendService     = ctx().services.require<IRenderBackendService>();
        auto& worldService       = ctx().services.require<IWorldService>();
        auto& camService         = ctx().services.require<ICameraService>();
        auto& gpuResourceService = ctx().services.require<IGpuResourceService>();
        auto& assetService       = ctx().services.require<IAssetService>();
        auto& shaderService      = ctx().services.require<IShaderService>();
        auto& window             = ctx().services.require<IWindowService>().window();

        // Optional ImGui service for rendering ImGui on top of frame.
        auto* imguiService = ctx().services.tryGet<IImGuiService>();

        // Optional frame debugger service for GPU capture.
        auto* frameDebuggerService = ctx().services.tryGet<IFrameDebuggerService>();

        auto& rd = backendService.renderDevice();

        RuntimeProfiler::setExternalSink(&m_RuntimeProfiler);
        if (m_PendingRenderPipelineReload)
        {
            if (m_PendingRenderPipelineAsset.empty())
                reloadRenderPipelineNow();
            else
                reloadRenderPipelineNow(m_PendingRenderPipelineAsset, m_PendingRenderPipelineRendererKey);
        }
        if (m_PendingRenderGraphUpdate)
            updateRenderGraphNow(m_PendingRenderGraphUpdateAsset, m_PendingRenderGraphUpdateRendererKey);

        m_RuntimeProfiler.beginFrame(m_FrameCounter);
        m_LastFrameGraphSnapshot.clear();
        if (m_FrameGraphTextureCaptureEnabled || m_FrameGraphTextureDumpCaptureFrames > 0)
            m_FrameGraphDebugTextures.clear();
        {
            constexpr uint64_t kDebugTextureReleaseDelayFrames = 8u;
            std::size_t        out                             = 0;
            for (auto& slot : m_RetiredFrameGraphDebugTextureSlots)
            {
                if (m_FrameCounter > slot.lastTouchedFrame + kDebugTextureReleaseDelayFrames)
                {
                    slot.texture.reset();
                }
                else
                {
                    m_RetiredFrameGraphDebugTextureSlots[out++] = std::move(slot);
                }
            }
            m_RetiredFrameGraphDebugTextureSlots.resize(out);
        }
        m_RuntimeProfiler.setVsyncEnabled(ctx().config.render.vSyncConfig != rhi::VerticalSync::eDisabled);
        rhi::CommandBuffer::resetFrameStats();

        // Begin frame first so downstream systems can consume per-frame backend state (e.g. XR eye views).
        if (!backendService.beginFrame())
        {
            m_SkipRender = true;
            m_RuntimeProfiler.endFrame();
            return;
        }

        // The frame slot was just acquired (its prior GPU work is complete): advance the deferred
        // deletion queue and free resources whose owners were released enough frames ago that the
        // GPU can no longer reference them.
        rhi::DeferredDeletionQueue::get().beginFrame();

        auto&                  cb = backendService.commandBuffer();
        RuntimeProfiler::Scope scopeRenderFrame {m_RuntimeProfiler, "RenderSystem::renderFrame"};
        const bool profilerCaptureEnabled = m_RuntimeProfiler.isEnabled();
        const bool gpuTimingEnabled       = profilerCaptureEnabled && !isTrackyGpuProfilerEnabled();
        if (gpuTimingEnabled)
        {
            rd.beginFrameGpuQuery(cb);
        }

        // Default target for cameras without explicit RT
        auto& defaultTarget = backendService.backbuffer();

        if (frameDebuggerService)
        {
            frameDebuggerService->captureStart();
        }

        World&                              world         = worldService.world();
        const auto                          cookedCameras = camService.cameras();
        const std::span<const RenderCamera> cams          = cookedCameras;
        const bool rayTracingSceneRequired = std::any_of(cams.begin(), cams.end(), [this](const RenderCamera& cam) {
            return rendererRequiresRayTracingScene(cam.rendererKey);
        });
        const bool rayTracingAvailable =
            rayTracingSceneRequired &&
            HasFlagValues(rd.getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline);

        // Asset upload/update stage (main thread)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "AssetService::update"};
            assetService.update(m_FrameCounter);
        }
        // Cook render instances
        RenderWorldCooker cooker {};
        auto*             timingService     = ctx().services.tryGet<ITimingService>();
        const float       renderTimeSeconds =
            timingService ? timingService->totalTime() : static_cast<float>(m_FrameCounter) / 60.0f;
        const float renderDeltaSeconds = timingService ? timingService->updateDeltaTime() : 0.0f;
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "RenderWorldCooker::cook"};
            cooker.cook(world,
                        assetService,
                        gpuResourceService,
                        rd,
                        m_GeometryFactory,
                        m_RenderWorldBack,
                        &shaderService,
                        renderTimeSeconds,
                        &m_GlyphAtlas);
        }
        m_RenderWorldBack.frameIndex = m_FrameCounter;

        const bool gaussianOrderedClodMode = m_GaussianSplatSettings.orderedClodEnabled();

        const uint64_t resourceRevision = gpuResourceService.contentRevision();
        const auto&    pool             = gpuResourceService.pool();

        uint32_t maxGeneralGaussianSplatPoints      = 0;
        uint32_t maxGeneralGaussianSplatSourceCount = 0;
        for (const auto& splatInst : m_RenderWorldBack.gaussianSplats)
        {
            if (splatInst.splatIndex >= pool.gaussianSplats.size())
                continue;
            const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
            maxGeneralGaussianSplatPoints += gpuSplat.pointCount;
            maxGeneralGaussianSplatSourceCount += gpuSplat.pointCount;
        }

        GaussianSplatFrameStats gaussianStats {};
        gaussianStats.frameIndex                      = m_FrameCounter;
        gaussianStats.baselineMode                    = m_GaussianSplatSettings.baselineMode;
        gaussianStats.foveatedRenderMode              = m_GaussianSplatSettings.foveatedRenderMode;
        gaussianStats.lodBudgetEnabled                = m_GaussianSplatSettings.lodBudgetEnabled();
        gaussianStats.foveatedClodEnabled             = m_GaussianSplatSettings.foveatedClodActive();
        gaussianStats.foveatedLayeredCompositeEnabled = m_GaussianSplatSettings.foveatedLayeredCompositeActive();
        gaussianStats.foveatedBudgetControllerEnabled = m_GaussianSplatSettings.foveatedBudgetControllerEnabled;
        gaussianStats.lodBudget                       = m_GaussianSplatSettings.lodBudget;
        gaussianStats.foveatedRingLevels              = m_GaussianSplatSettings.foveatedRingLevels;
        gaussianStats.foveatedResolutionScales        = m_GaussianSplatSettings.foveatedResolutionScales;
        gaussianStats.foveatedRingDegrees             = m_GaussianSplatSettings.foveatedRingDegrees;
        gaussianStats.foveatedTargetFrameMs           = m_GaussianSplatSettings.foveatedTargetFrameMs;
        gaussianStats.splatAssets                     = static_cast<uint32_t>(m_RenderWorldBack.gaussianSplats.size());
        gaussianStats.totalSplats                     = maxGeneralGaussianSplatPoints;

        const bool gaussianModeSettingsDirty =
            m_GaussianSplatSettings.baselineMode != m_AppliedGaussianSplatSettings.baselineMode;
        const bool gaussianSelectionSettingsDirty =
            gaussianSplatSelectionSettingsDirty(m_GaussianSplatSettings, m_AppliedGaussianSplatSettings);
        bool gpuSceneTopologyDirty =
            gaussianModeSettingsDirty || m_GpuSceneDirtyTracker.shouldRebuildTopology(
                                             m_RenderWorldBack, resourceRevision, m_EnableGpuDrivenMeshletPipeline);
        bool gpuSceneTransformDirty =
            !gpuSceneTopologyDirty && m_GpuSceneDirtyTracker.shouldUpdateTransforms(m_RenderWorldBack);
        if (gpuSceneTransformDirty &&
            (m_GpuSceneDatabaseFront.transforms.size() != m_RenderWorldBack.instances.size() ||
             m_GpuSceneDatabaseFront.instances.size() != m_RenderWorldBack.instances.size()))
        {
            gpuSceneTopologyDirty  = true;
            gpuSceneTransformDirty = false;
        }
        bool gpuSceneSkinDirty = false;
        if (!gpuSceneTopologyDirty && hasSkinMatrices(m_RenderWorldBack))
        {
            if (refreshSkinMatricesForExistingGpuScene(m_RenderWorldBack, m_GpuSceneDatabaseFront))
            {
                gpuSceneSkinDirty = true;
            }
            else
            {
                gpuSceneTopologyDirty  = true;
                gpuSceneTransformDirty = false;
            }
        }
        if (!gpuSceneTopologyDirty && !gpuSceneTransformDirty && rayTracingAvailable &&
            !m_RenderWorldBack.instances.empty() &&
            (!m_GpuSceneDatabaseFront.rayTracingTlas || !m_GpuSceneDatabaseFront.rayTracingInstanceBuffer ||
             !m_GpuSceneDatabaseFront.rayTracingGeometryNodeBuffer))
        {
            gpuSceneTopologyDirty = true;
        }
        const bool gpuSceneDirty          = gpuSceneTopologyDirty || gpuSceneTransformDirty || gpuSceneSkinDirty;
        const bool gaussianSelectionDirty = gaussianOrderedClodMode && gaussianSelectionSettingsDirty;

        // Build GPU scene database + per-view draw state.
        //
        // Database layer:
        // - stable pointer to global resource pool
        // - scene/instance tables
        //
        // View layer:
        // - draw table
        // - indirect commands
        if (gpuSceneTransformDirty)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GpuScene::update_transforms"};

            auto& gpuSceneDatabase = m_GpuSceneDatabaseFront;
            auto& gpuSceneView     = m_GpuSceneViewFront;

            for (uint32_t instanceIndex = 0; instanceIndex < static_cast<uint32_t>(m_RenderWorldBack.instances.size());
                 ++instanceIndex)
            {
                const auto& model                          = m_RenderWorldBack.instances[instanceIndex].worldMatrix;
                gpuSceneDatabase.transforms[instanceIndex] = model;
            }
            gpuSceneDatabase.uploadTransforms(rd, cb);
            if (rayTracingAvailable)
            {
                if (gpuSceneDatabase.rayTracingInstances.empty() && !gpuSceneDatabase.instances.empty())
                    gpuSceneDatabase.rebuildRayTracingScene(rd);
                else
                    gpuSceneDatabase.rebuildRayTracingTlas(rd);
            }

            if (gpuSceneView.isCpuDriven())
            {
                for (auto& draw : gpuSceneView.draws)
                {
                    if (draw.instanceIndex < m_RenderWorldBack.instances.size())
                        draw.model = m_RenderWorldBack.instances[draw.instanceIndex].worldMatrix;
                }
                gpuSceneView.uploadDraws(rd, cb);
            }

            uint32_t gaussianDrawIndex = 0;
            for (const auto& splatInst : m_RenderWorldBack.gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                if (gaussianDrawIndex >= gpuSceneView.generalGaussianSplatDraws.size())
                    break;

                gpuSceneView.generalGaussianSplatDraws[gaussianDrawIndex].model = splatInst.worldMatrix;
                ++gaussianDrawIndex;
            }

            if (!gpuSceneView.generalGaussianSplatDraws.empty() && gpuSceneView.generalGaussianSplatDrawBuffer)
            {
                cb.update(*gpuSceneView.generalGaussianSplatDrawBuffer,
                          0,
                          static_cast<uint64_t>(gpuSceneView.generalGaussianSplatDraws.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatDrawRecord),
                          gpuSceneView.generalGaussianSplatDraws.data());
            }

            if (gpuSceneView.generalGaussianSplatVisibleCountBuffer)
            {
                const uint32_t zero = 0u;
                cb.update(*gpuSceneView.generalGaussianSplatVisibleCountBuffer, 0, sizeof(uint32_t), &zero);
            }

            if (gpuSceneView.generalGaussianSplatDispatchArgsBuffer)
            {
                const uint32_t zeroArgs[4] = {0u, 1u, 1u, 0u};
                cb.update(*gpuSceneView.generalGaussianSplatDispatchArgsBuffer, 0, sizeof(zeroArgs), zeroArgs);
            }

            resetGaussianSplatIndirectBuffers(rd, gpuSceneView);

            m_RenderWorldBack.gpuSceneDatabase = &m_GpuSceneDatabaseFront;
            m_RenderWorldBack.gpuSceneView     = &m_GpuSceneViewFront;
        }
        if (gpuSceneSkinDirty && !gpuSceneTopologyDirty)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GpuScene::update_skin_matrices"};
            m_GpuSceneDatabaseFront.uploadSkinMatrices(rd, cb);
        }
        else if (gpuSceneTopologyDirty)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GpuScene::rebuild"};
            auto                   packGaussianCovariance = [](const glm::uvec4 packed) {
                const glm::vec2 p0 = glm::unpackHalf2x16(packed.x);
                const glm::vec2 p1 = glm::unpackHalf2x16(packed.y);
                const glm::vec2 p2 = glm::unpackHalf2x16(packed.z);

                glm::mat3 sigma(0.0f);
                sigma[0][0] = p0.x;
                sigma[1][0] = p0.y;
                sigma[0][1] = p0.y;
                sigma[2][0] = p1.x;
                sigma[0][2] = p1.x;
                sigma[1][1] = p1.y;
                sigma[2][1] = p2.x;
                sigma[1][2] = p2.x;
                sigma[2][2] = p2.y;
                return sigma;
            };
            auto repackGaussianCovariance = [](const glm::mat3& sigma) {
                return glm::uvec4 {
                    glm::packHalf2x16(glm::vec2(sigma[0][0], sigma[1][0])),
                    glm::packHalf2x16(glm::vec2(sigma[2][0], sigma[1][1])),
                    glm::packHalf2x16(glm::vec2(sigma[2][1], sigma[2][2])),
                    0u,
                };
            };

            m_GpuSceneDatabaseBack.beginFrame(pool);
            m_GpuSceneDatabaseBack.instances.reserve(m_RenderWorldBack.instances.size());
            m_GpuSceneDatabaseBack.transforms.reserve(m_RenderWorldBack.instances.size());
            m_GpuSceneDatabaseBack.rebuildMeshTableFromResources();

            // Keep CPU staging mirrors even though the current render path is still
            // CPU-driven. The upcoming GPU-driven cluster pipeline will consume the
            // same scene database buffers directly.
            for (auto& inst : m_RenderWorldBack.instances)
            {
                const uint32_t transformIndex = m_GpuSceneDatabaseBack.pushTransform(inst.worldMatrix);
                inst.skinMatrixOffset = m_GpuSceneDatabaseBack.pushSkinMatrices(inst.skinMatrices);

                resource::GpuInstance gpuInst {};
                gpuInst.meshIndex      = inst.meshIndex;
                gpuInst.materialIndex  = inst.materialIndex;
                gpuInst.transformIndex = transformIndex;
                gpuInst.flags          = 0;
                gpuInst.entityPickingId = makeEntityPickingId(inst.entity);
                gpuInst.skinMatrixOffset = inst.skinMatrixOffset;
                gpuInst.skinMatrixCount  = inst.skinMatrixCount;
                m_GpuSceneDatabaseBack.pushInstance(gpuInst);
            }
            m_GpuSceneDatabaseBack.uploadSceneTables(rd, cb);

            if (rayTracingAvailable)
                m_GpuSceneDatabaseBack.rebuildRayTracingScene(rd);

            uint32_t maxMeshletDraws = 0;
            for (const auto& inst : m_RenderWorldBack.instances)
            {
                if (inst.meshIndex >= pool.meshes.size())
                    continue;
                maxMeshletDraws += pool.meshes[inst.meshIndex].meshletCount;
            }

            if (m_EnableGpuDrivenMeshletPipeline)
            {
                m_GpuSceneViewBack.beginFrame(m_GpuSceneDatabaseBack, resource::GpuSceneBuildMode::eGpuDriven);
                m_GpuSceneViewBack.setGpuDrivenCaps(
                    static_cast<uint32_t>(m_GpuSceneDatabaseBack.instances.size()), maxMeshletDraws, maxMeshletDraws);
            }
            else
            {
                m_GpuSceneViewBack.beginFrame(m_GpuSceneDatabaseBack, resource::GpuSceneBuildMode::eCpuDriven);
                m_GpuSceneViewBack.setGpuDrivenCaps(
                    static_cast<uint32_t>(m_GpuSceneDatabaseBack.instances.size()), maxMeshletDraws, maxMeshletDraws);
                m_GpuSceneViewBack.ensureVisibleMeshletBuffers(rd);
                m_GpuSceneViewBack.draws.reserve(maxMeshletDraws);

                for (uint32_t instanceIndex = 0;
                     instanceIndex < static_cast<uint32_t>(m_RenderWorldBack.instances.size());
                     ++instanceIndex)
                {
                    const auto& inst = m_RenderWorldBack.instances[instanceIndex];
                    if (inst.meshIndex >= pool.meshes.size())
                        continue;
                    if (instanceIndex >= m_GpuSceneDatabaseBack.instances.size())
                        continue;

                    const auto& mesh = pool.meshes[inst.meshIndex];
                    if (mesh.meshletCount == 0)
                        continue;

                    for (uint32_t localMeshlet = 0; localMeshlet < mesh.meshletCount; ++localMeshlet)
                    {
                        const uint32_t globalMeshletIndex = mesh.meshletOffset + localMeshlet;
                        if (globalMeshletIndex >= pool.meshlets.cpuMeshlets.size())
                            continue;

                        const auto& meshlet = pool.meshlets.cpuMeshlets[globalMeshletIndex];

                        resource::GpuDrawRecord dr;
                        dr.primitiveIndex       = globalMeshletIndex;
                        dr.materialIndex        = remapMaterialIndex(inst, mesh, meshlet.materialIndex);
                        dr.vertexStrideBytes    = mesh.vertexStrideBytes;
                        dr.flags                = resource::gpuDrawFlagsToMask(resource::GpuDrawFlags::eMeshlet);
                        dr.vertexAddress        = pool.geometry.vertexBytesAddress;
                        dr.instanceIndex        = instanceIndex;
                        const auto layout       = resource::inspectGpuVertexLayout(mesh.vertexAttributes);
                        dr.vertexAttributeMask  = layout.attributeMask;
                        dr.positionOffsetBytes  = layout.positionOffsetBytes;
                        dr.normalOffsetBytes    = layout.normalOffsetBytes;
                        dr.colorOffsetBytes     = layout.colorOffsetBytes;
                        dr.texCoord0OffsetBytes = layout.texCoord0OffsetBytes;
                        dr.texCoord1OffsetBytes = layout.texCoord1OffsetBytes;
                        dr.tangentOffsetBytes   = layout.tangentOffsetBytes;
                        dr.jointIndicesOffsetBytes = layout.jointIndicesOffsetBytes;
                        dr.jointWeightsOffsetBytes = layout.jointWeightsOffsetBytes;
                        dr.skinMatrixOffset = m_GpuSceneDatabaseBack.instances[instanceIndex].skinMatrixOffset;
                        dr.skinMatrixCount  = m_GpuSceneDatabaseBack.instances[instanceIndex].skinMatrixCount;
                        dr.entityPickingId      = makeEntityPickingId(inst.entity);
                        dr.model                = inst.worldMatrix;
                        m_GpuSceneViewBack.pushMeshletDraw(std::move(dr));
                    }
                }

                std::stable_sort(
                    m_GpuSceneViewBack.draws.begin(), m_GpuSceneViewBack.draws.end(), [](const auto& a, const auto& b) {
                        if (a.materialIndex != b.materialIndex)
                            return a.materialIndex < b.materialIndex;
                        return a.primitiveIndex < b.primitiveIndex;
                    });

                m_GpuSceneViewBack.uploadDraws(rd, cb);
                m_GpuSceneViewBack.buildIndirectFromDraws(pool);
                m_GpuSceneViewBack.uploadIndirect(rd);
            }

            m_GpuSceneViewBack.generalGaussianSplatDraws.clear();
            m_GpuSceneViewBack.generalGaussianSplatPackedSources.clear();
            m_GpuSceneViewBack.generalGaussianSplatSelectedSources.clear();
            m_GpuSceneViewBack.generalGaussianSplatDirectPrefix = false;
            m_GpuSceneViewBack.generalGaussianSplatDraws.reserve(m_RenderWorldBack.gaussianSplats.size());
            m_GpuSceneViewBack.generalGaussianSplatPackedSources.reserve(maxGeneralGaussianSplatSourceCount);

            for (const auto& splatInst : m_RenderWorldBack.gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                const uint32_t drawIndex = static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatDraws.size());
                const uint32_t pointBase = gpuSplat.pointOffset;
                const uint32_t shBaseStride = std::max(gpuSplat.shRestCoeffCount, 1u);
                const uint32_t rawSourceOffset =
                    static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatPackedSources.size());

                resource::GpuGeneralGaussianSplatDrawRecord drawRecord {};
                drawRecord.splatIndex  = splatInst.splatIndex;
                drawRecord.pointOffset = rawSourceOffset;
                drawRecord.pointCount  = gpuSplat.pointCount;
                drawRecord.shDegree    = static_cast<uint32_t>(std::max(gpuSplat.shDegree, 0));
                // x: kernel size, y: cutoff scale, z: opacity scale, w: reserved sort order.
                drawRecord.params0 = glm::vec4 {0.3f, 1.0f, 1.0f, 0.0f};
                drawRecord.model   = splatInst.worldMatrix;

                for (uint32_t localPoint = 0; localPoint < gpuSplat.pointCount; ++localPoint)
                {
                    const uint32_t                                globalPoint = pointBase + localPoint;
                    resource::GpuGeneralGaussianSplatPackedSource packed {};
                    if (globalPoint < pool.gaussianStorage.cpuCenters.size() &&
                        globalPoint < pool.gaussianStorage.cpuCovariances.size() &&
                        globalPoint < pool.gaussianStorage.cpuColors.size())
                    {
                        const glm::vec4  localCenter = pool.gaussianStorage.cpuCenters[globalPoint];
                        const uint32_t   shOffset    = globalPoint * shBaseStride;
                        const glm::uvec2 sh0         = shOffset < pool.gaussianStorage.cpuSh.size() ?
                                                           pool.gaussianStorage.cpuSh[shOffset] :
                                                           glm::uvec2 {0u};

                        packed.posOpacity = glm::uvec4 {
                            std::bit_cast<uint32_t>(localCenter.x),
                            std::bit_cast<uint32_t>(localCenter.y),
                            std::bit_cast<uint32_t>(localCenter.z),
                            pool.gaussianStorage.cpuColors[globalPoint].y,
                        };
                        packed.covariance0 = pool.gaussianStorage.cpuCovariances[globalPoint];
                        packed.colorSh0    = glm::uvec4 {
                            pool.gaussianStorage.cpuColors[globalPoint].x,
                            pool.gaussianStorage.cpuColors[globalPoint].y,
                            sh0.x,
                            sh0.y,
                        };
                        packed.aux0 = glm::uvec4 {globalPoint, 0u, shOffset, 0u};
                    }
                    m_GpuSceneViewBack.pushGeneralGaussianSplatSource(packed);
                }

                m_GpuSceneViewBack.pushGeneralGaussianSplatDraw(drawRecord);
            }

            uint32_t       selectedSourceCapacity = 0u;
            uint32_t       activeGaussianSplats   = 0u;
            const uint32_t packedGaussianSources =
                static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatPackedSources.size());
            const bool gaussianDirectPrefix =
                gaussianOrderedClodMode && m_GpuSceneViewBack.generalGaussianSplatDraws.size() == 1u;
            gaussianStats.directPrefix = gaussianDirectPrefix;
            if (gaussianDirectPrefix)
            {
                selectedSourceCapacity = 0u;
                activeGaussianSplats =
                    std::min(packedGaussianSources,
                             effectiveGaussianLodBudget(m_GaussianSplatSettings, maxGeneralGaussianSplatPoints));
                gaussianStats.lodSelectedRawSplats = activeGaussianSplats;
            }
            else if (gaussianOrderedClodMode)
            {
                rebuildGaussianSplatOrderedClodPrefixSources(
                    m_GpuSceneViewBack, m_RenderWorldBack.gaussianSplats, pool, m_RuntimeProfiler);
                selectedSourceCapacity =
                    static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatSelectedSources.size());
                activeGaussianSplats =
                    std::min(selectedSourceCapacity,
                             effectiveGaussianLodBudget(m_GaussianSplatSettings, maxGeneralGaussianSplatPoints));
                gaussianStats.lodSelectedRawSplats = activeGaussianSplats;
            }
            else
            {
                rebuildGaussianSplatSelectedSources(
                    m_GpuSceneViewBack, m_RenderWorldBack.gaussianSplats, pool, gaussianStats, m_RuntimeProfiler);
                selectedSourceCapacity =
                    static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatSelectedSources.size());
                activeGaussianSplats = selectedSourceCapacity;
            }

            uint32_t maxVisibleGaussianSplats = activeGaussianSplats;
            if (m_GaussianSplatSettings.lodBudgetEnabled() && m_GaussianSplatSettings.lodBudget > 0u)
            {
                maxVisibleGaussianSplats = std::min(maxVisibleGaussianSplats, m_GaussianSplatSettings.lodBudget);
            }

            gaussianStats.drawRecords    = static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatDraws.size());
            gaussianStats.preparedSplats = activeGaussianSplats;
            gaussianStats.maxVisibleSplatCap = maxVisibleGaussianSplats;
            m_GaussianSplatStats             = gaussianStats;

            m_GpuSceneViewBack.setGeneralGaussianSplatCaps(gaussianStats.drawRecords,
                                                           packedGaussianSources,
                                                           selectedSourceCapacity,
                                                           activeGaussianSplats,
                                                           maxVisibleGaussianSplats,
                                                           gaussianDirectPrefix);
            applyGaussianSplatFoveatedClodSettings(m_GpuSceneViewBack, m_GaussianSplatSettings);
            m_GpuSceneViewBack.generalGaussianSplatShBuffer = pool.gaussianStorage.shBuffer;
            m_GpuSceneViewBack.ensureGeneralGaussianSplatBuffers(rd);

            if (!m_GpuSceneViewBack.generalGaussianSplatDraws.empty() &&
                m_GpuSceneViewBack.generalGaussianSplatDrawBuffer)
            {
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatDrawBuffer,
                          0,
                          static_cast<uint64_t>(m_GpuSceneViewBack.generalGaussianSplatDraws.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatDrawRecord),
                          m_GpuSceneViewBack.generalGaussianSplatDraws.data());
            }

            if (!m_GpuSceneViewBack.generalGaussianSplatPackedSources.empty() &&
                m_GpuSceneViewBack.generalGaussianSplatPackedSourceBuffer)
            {
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatPackedSourceBuffer,
                          0,
                          static_cast<uint64_t>(m_GpuSceneViewBack.generalGaussianSplatPackedSources.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatPackedSource),
                          m_GpuSceneViewBack.generalGaussianSplatPackedSources.data());
            }

            if (!m_GpuSceneViewBack.generalGaussianSplatSelectedSources.empty() &&
                m_GpuSceneViewBack.generalGaussianSplatSelectedSourceBuffer)
            {
                RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GaussianLOD::UploadSelected"};
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatSelectedSourceBuffer,
                          0,
                          static_cast<uint64_t>(m_GpuSceneViewBack.generalGaussianSplatSelectedSources.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatSelectedSource),
                          m_GpuSceneViewBack.generalGaussianSplatSelectedSources.data());
            }

            if (m_GpuSceneViewBack.generalGaussianSplatVisibleCountBuffer)
            {
                const uint32_t zero = 0u;
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatVisibleCountBuffer, 0, sizeof(uint32_t), &zero);
            }

            if (m_GpuSceneViewBack.generalGaussianSplatDispatchArgsBuffer)
            {
                const uint32_t zeroArgs[4] = {0u, 1u, 1u, 0u};
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatDispatchArgsBuffer, 0, sizeof(zeroArgs), zeroArgs);
            }

            resetGaussianSplatIndirectBuffers(rd, m_GpuSceneViewBack);

            m_RenderWorldBack.gpuSceneDatabase = &m_GpuSceneDatabaseBack;
            m_RenderWorldBack.gpuSceneView     = &m_GpuSceneViewBack;
        }
        else
        {
            // Reuse previous snapshot when neither cooked world nor resource pool changed.
            m_RenderWorldBack.gpuSceneDatabase = &m_GpuSceneDatabaseFront;
            m_RenderWorldBack.gpuSceneView     = &m_GpuSceneViewFront;
        }

        if (!gpuSceneDirty && gaussianSelectionDirty)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GpuScene::gaussian_lod_selection"};
            auto&                  gpuSceneView = m_GpuSceneViewFront;

            uint32_t selectedSourceCapacity =
                static_cast<uint32_t>(gpuSceneView.generalGaussianSplatSelectedSources.size());
            uint32_t   activeGaussianSplats  = 0u;
            bool       uploadSelectedSources = false;
            const bool gaussianDirectPrefix  = gpuSceneView.generalGaussianSplatDirectPrefix;
            gaussianStats.directPrefix       = gaussianDirectPrefix;
            if (!gaussianDirectPrefix && selectedSourceCapacity < maxGeneralGaussianSplatPoints)
            {
                rebuildGaussianSplatOrderedClodPrefixSources(
                    gpuSceneView, m_RenderWorldBack.gaussianSplats, pool, m_RuntimeProfiler);
                selectedSourceCapacity = static_cast<uint32_t>(gpuSceneView.generalGaussianSplatSelectedSources.size());
                uploadSelectedSources  = true;
            }

            const uint32_t activeBudgetSourceCount =
                gaussianDirectPrefix ? static_cast<uint32_t>(gpuSceneView.generalGaussianSplatPackedSources.size()) :
                                       selectedSourceCapacity;
            activeGaussianSplats =
                std::min(activeBudgetSourceCount,
                         effectiveGaussianLodBudget(m_GaussianSplatSettings, maxGeneralGaussianSplatPoints));
            gaussianStats.lodSelectedRawSplats = activeGaussianSplats;

            uint32_t maxVisibleGaussianSplats = activeGaussianSplats;
            if (m_GaussianSplatSettings.lodBudgetEnabled() && m_GaussianSplatSettings.lodBudget > 0u)
            {
                maxVisibleGaussianSplats = std::min(maxVisibleGaussianSplats, m_GaussianSplatSettings.lodBudget);
            }

            gaussianStats.drawRecords        = static_cast<uint32_t>(gpuSceneView.generalGaussianSplatDraws.size());
            gaussianStats.preparedSplats     = activeGaussianSplats;
            gaussianStats.maxVisibleSplatCap = maxVisibleGaussianSplats;
            m_GaussianSplatStats             = gaussianStats;

            gpuSceneView.setGeneralGaussianSplatCaps(
                gaussianStats.drawRecords,
                static_cast<uint32_t>(gpuSceneView.generalGaussianSplatPackedSources.size()),
                selectedSourceCapacity,
                activeGaussianSplats,
                maxVisibleGaussianSplats,
                gaussianDirectPrefix);
            applyGaussianSplatFoveatedClodSettings(gpuSceneView, m_GaussianSplatSettings);
            gpuSceneView.generalGaussianSplatShBuffer = pool.gaussianStorage.shBuffer;
            gpuSceneView.ensureGeneralGaussianSplatBuffers(rd);

            if (uploadSelectedSources && !gpuSceneView.generalGaussianSplatSelectedSources.empty() &&
                gpuSceneView.generalGaussianSplatSelectedSourceBuffer)
            {
                RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GaussianLOD::UploadSelected"};
                cb.update(*gpuSceneView.generalGaussianSplatSelectedSourceBuffer,
                          0,
                          static_cast<uint64_t>(gpuSceneView.generalGaussianSplatSelectedSources.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatSelectedSource),
                          gpuSceneView.generalGaussianSplatSelectedSources.data());
            }

            if (gpuSceneView.generalGaussianSplatVisibleCountBuffer)
            {
                const uint32_t zero = 0u;
                cb.update(*gpuSceneView.generalGaussianSplatVisibleCountBuffer, 0, sizeof(uint32_t), &zero);
            }

            if (gpuSceneView.generalGaussianSplatDispatchArgsBuffer)
            {
                const uint32_t zeroArgs[4] = {0u, 1u, 1u, 0u};
                cb.update(*gpuSceneView.generalGaussianSplatDispatchArgsBuffer, 0, sizeof(zeroArgs), zeroArgs);
            }

            resetGaussianSplatIndirectBuffers(rd, gpuSceneView);
        }

        m_GpuSceneDirtyTracker.markBuilt(m_RenderWorldBack, resourceRevision, m_EnableGpuDrivenMeshletPipeline);
        if (gpuSceneDirty || gaussianSelectionDirty)
        {
            m_AppliedGaussianSplatSettings = m_GaussianSplatSettings;
        }

        std::swap(m_RenderWorldFront, m_RenderWorldBack);
        if (gpuSceneTopologyDirty)
        {
            std::swap(m_GpuSceneDatabaseFront, m_GpuSceneDatabaseBack);
            std::swap(m_GpuSceneViewFront, m_GpuSceneViewBack);
        }
        m_RenderWorldFront.gpuSceneDatabase = &m_GpuSceneDatabaseFront;
        m_RenderWorldFront.gpuSceneView     = &m_GpuSceneViewFront;
        m_RenderWorldBack.gpuSceneDatabase  = &m_GpuSceneDatabaseBack;
        m_RenderWorldBack.gpuSceneView      = &m_GpuSceneViewBack;

        constexpr uint64_t kOverrideRenderWorldReleaseDelayFrames = 4u;
        std::size_t        overrideOut                            = 0;
        for (auto& slot : m_OverrideRenderWorlds)
        {
            if (!slot.world || m_FrameCounter > slot.lastTouchedFrame + kOverrideRenderWorldReleaseDelayFrames)
            {
                slot = {};
            }
            else
            {
                m_OverrideRenderWorlds[overrideOut++] = std::move(slot);
            }
        }
        m_OverrideRenderWorlds.resize(overrideOut);

        for (const auto& cam : cams)
        {
            if (!cam.worldOverride)
                continue;
            auto slotIt =
                std::find_if(m_OverrideRenderWorlds.begin(),
                             m_OverrideRenderWorlds.end(),
                             [&](const OverrideRenderWorldSlot& slot) { return slot.world == cam.worldOverride; });
            if (slotIt == m_OverrideRenderWorlds.end())
            {
                slotIt        = m_OverrideRenderWorlds.insert(m_OverrideRenderWorlds.end(), OverrideRenderWorldSlot {});
                slotIt->world = cam.worldOverride;
            }
            auto& slot            = *slotIt;
            slot.lastTouchedFrame = m_FrameCounter;
            {
                RuntimeProfiler::Scope scope {m_RuntimeProfiler, "RenderWorldCooker::cook_override"};
                const float cookTimeSeconds = cam.overrideFrameTime ? cam.frameTimeSeconds : renderTimeSeconds;
                cooker.cook(*slot.world,
                            assetService,
                            gpuResourceService,
                            rd,
                            m_GeometryFactory,
                            slot.renderWorld,
                            &shaderService,
                            cookTimeSeconds,
                            &m_GlyphAtlas);
            }
            slot.renderWorld.frameIndex = m_FrameCounter;
            buildCpuDrivenGpuSceneForRenderWorld(
                slot.renderWorld, slot.gpuSceneDatabase, slot.gpuSceneView, pool, rd, cb);
        }

        // Advance GPU particle pools once per frame and publish their per-emitter draw records onto
        // each render world's GpuSceneView for the render-graph particle passes to consume. Emission
        // is advanced at most once per emitter per frame, so the editor scene view (main world) and
        // game view (an override world wrapping the same World) both render the same simulation.
        m_ParticleManager.update(m_RenderWorldFront, rd, renderDeltaSeconds, m_FrameCounter);
        for (auto& particleSlot : m_OverrideRenderWorlds)
            m_ParticleManager.update(particleSlot.renderWorld, rd, renderDeltaSeconds, m_FrameCounter);

        m_FrameResources.beginFrame(m_FrameCounter);
        {
            ImmediateResourceUploader frameUploader {m_FrameResources, rd};
            prepareFrameData(frameUploader, m_PreparedFrameData, m_FrameCounter, renderTimeSeconds, renderDeltaSeconds);
        }

        ++m_FrameCounter;

        std::vector<size_t> cameraOrder(cams.size());
        std::iota(cameraOrder.begin(), cameraOrder.end(), 0u);
        std::stable_sort(cameraOrder.begin(), cameraOrder.end(), [&cams](size_t a, size_t b) {
            return cams[a].priority < cams[b].priority;
        });

        const bool supportsMultiview =
            HasFlagValues(rd.getFeatureReport().flags, rhi::RenderDeviceFeatureReportFlagBits::eMultiview);
        const auto                        xrEyeViews               = backendService.xrEyeViews();
        bool                              skipRemainingStereoViews = false;
        std::unordered_set<rhi::Texture*> clearedTargetsThisFrame;
        m_RuntimeProfiler.setGpuScopeCpuFallback(rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU);

        if (!profilerCaptureEnabled)
        {
            m_RuntimeProfiler.setGpuScopeCallbacks({}, {}, {});
            rhi::setBuiltinProfilerGpuScopeCallbacks({}, {});
        }
        else if (isTrackyGpuProfilerEnabled())
        {
            m_RuntimeProfiler.setGpuScopeCallbacks(
                []() { return uint64_t {0}; }, [](const uint64_t) {}, [](const uint64_t) { return -1.0; });
            rhi::setBuiltinProfilerGpuScopeCallbacks(
                [](const rhi::BuiltinProfilerGpuScopeContext& ctx) { g_CurrentBuiltinProfilerGpuScopeContext = ctx; },
                [this](const rhi::BuiltinProfilerGpuScopeContext& ctx, const char* label) {
                    g_CurrentBuiltinProfilerGpuScopeContext = ctx;
                    const char* scopeLabel = label ? label : "GPU Scope";
                    (void)m_RuntimeProfiler.beginScope(scopeLabel);
                    (void)m_RuntimeProfiler.beginGpuScope(scopeLabel);
                },
                [this](const rhi::BuiltinProfilerGpuScopeContext& ctx) {
                    g_CurrentBuiltinProfilerGpuScopeContext = ctx;
                    m_RuntimeProfiler.endGpuScope();
                    m_RuntimeProfiler.endScope();
                });
        }
        else
        {
            m_RuntimeProfiler.setGpuScopeCallbacks(
                [this, &rd, &cb]() {
                    if (g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle == 0)
                        return uint64_t {0};

                    if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                    {
                        return uint64_t {0};
                    }

                    // WebGPU compute encoders are kept open lazily. At a framegraph pass boundary the next
                    // top-level scope may still observe the previous compute pass as active, which would
                    // incorrectly suppress or mis-attribute the new pass timing.
                    if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU &&
                        g_CurrentBuiltinProfilerGpuScopeContext.renderPassEncoderHandle == 0 &&
                        g_CurrentBuiltinProfilerGpuScopeContext.computePassEncoderHandle != 0 &&
                        m_RuntimeProfiler.gpuScopeDepth() <= 1)
                    {
                        rhi::WebGPUCommandBufferAccess::closeActiveComputePassForProfilingBoundary(cb);
                        g_CurrentBuiltinProfilerGpuScopeContext.renderPassEncoderHandle =
                            cb.getCurrentRenderPassEncoderHandle();
                        g_CurrentBuiltinProfilerGpuScopeContext.computePassEncoderHandle =
                            cb.getCurrentComputePassEncoderHandle();
                    }

                    // WebGPU fallback timestamps are pass-bound; ignore nested scopes inside an active pass.
                    if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU &&
                        (g_CurrentBuiltinProfilerGpuScopeContext.renderPassEncoderHandle != 0 ||
                         g_CurrentBuiltinProfilerGpuScopeContext.computePassEncoderHandle != 0))
                    {
                        return uint64_t {0};
                    }
                    return rd.beginScopeGpuQuery(g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle);
                },
                [&rd](const uint64_t token) {
                    if (g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle == 0 || token == 0)
                        return;
                    if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                        return;
                    rd.endScopeGpuQuery(g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle, token);
                },
                [&rd](const uint64_t token) {
                    if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                        return -1.0;
                    return rd.consumeScopeGpuMs(token);
                });
            rhi::setBuiltinProfilerGpuScopeCallbacks(
                [](const rhi::BuiltinProfilerGpuScopeContext& ctx) { g_CurrentBuiltinProfilerGpuScopeContext = ctx; },
                [this](const rhi::BuiltinProfilerGpuScopeContext& ctx, const char* label) {
                    g_CurrentBuiltinProfilerGpuScopeContext = ctx;
                    const char* scopeLabel = label ? label : "GPU Scope";
                    (void)m_RuntimeProfiler.beginScope(scopeLabel);
                    (void)m_RuntimeProfiler.beginGpuScope(scopeLabel);
                },
                [this](const rhi::BuiltinProfilerGpuScopeContext& ctx) {
                    g_CurrentBuiltinProfilerGpuScopeContext = ctx;
                    m_RuntimeProfiler.endGpuScope();
                    m_RuntimeProfiler.endScope();
                });
        }

        // TODO: TimeSystem, for now use 0
        const fsec dt {0};
        static_cast<void>(dt);

        for (const size_t cameraIdx : cameraOrder)
        {
            RuntimeProfiler::Scope scopeCamera {m_RuntimeProfiler, "RenderCamera::execute"};
            const auto&            cam = cams[cameraIdx];

            if (skipRemainingStereoViews && cam.isXRView && !cam.isXRPrimaryView)
                continue;
            if (!cam.isXRView || cam.isXRPrimaryView)
                skipRemainingStereoViews = false;

            FrameGraph             fg {};
            FrameGraphBlackboard   bb {};
            FrameGraphDataRegistry dataRegistry {};

            // Resolved early (it is const/side-effect-free) so the stereo decision below can
            // consult the renderer's graph: a graph that names the two eyes itself
            // (explicit-per-eye) is rendered ONCE here instead of via 2-layer multiview.
            const auto renderer = resolveRenderer(cam);

            const bool canUseXrMultiview = supportsMultiview && cam.isXRView && cam.isXRPrimaryView &&
                                           cam.viewCount == 2u && !xrEyeViews.empty() && xrEyeViews[0].stereoTarget;
            const bool canUseLocalStereoTarget = supportsMultiview && cam.isXRView && cam.isXRPrimaryView &&
                                                 cam.viewCount == 2u && cam.target && cam.target->getNumLayers() >= 2u;
            // The graph drives each eye target itself; render the scene once (mono source) and
            // let the graph's per-eye composition write xrEyeTargets[0/1] (which are layer
            // views into the submitted array swapchain).
            const bool wantsExplicitPerEye = canUseXrMultiview && renderer && renderer->prefersExplicitPerEyeStereo();
            auto*      stereoTarget        = (canUseXrMultiview && !wantsExplicitPerEye) ?
                                                 xrEyeViews[0].stereoTarget :
                                                 (canUseLocalStereoTarget && !wantsExplicitPerEye ? cam.target : nullptr);

            rhi::Texture* target =
                stereoTarget ? stereoTarget :
                (wantsExplicitPerEye && !xrEyeViews.empty() && xrEyeViews[0].target ?
                     xrEyeViews[0].target :
                     (cam.target ? cam.target : &defaultTarget));
            if (!target)
                continue;

            const bool isBackbufferTarget = !cam.isXRView && cam.target == nullptr && target == &defaultTarget;
            const bool useWindowContentArea =
                isBackbufferTarget && window.platformType() == os::Window::PlatformType::eAndroidNativeWindow;
            const rhi::Rect2D  renderArea  = useWindowContentArea ?
                                                 window.getContentArea() :
                                                 rhi::Rect2D {.offset = {0, 0}, .extent = target->getExtent()};
            rhi::Extent2D sceneRenderExtent = renderArea.extent;
            if (auto* upscaler = ctx().services.tryGet<IRenderUpscalerService>(); upscaler != nullptr)
            {
                auto settings = upscaler->settings();
                auto* provider = upscaler->activeProvider();
                // XR multiview (stereoTarget) and per-eye fallback views participate: the graph
                // evaluates the upscaler once per eye with per-layer resource slices. Only
                // explicit-per-eye graphs drive their own eye composition and stay excluded.
                const bool canUseUpscalerRenderExtent =
                    provider != nullptr && settings.enabled && settings.mode != UpscalerMode::eOff &&
                    settings.mode != UpscalerMode::eDLAA && !wantsExplicitPerEye && cam.allowUpscaler &&
                    supportsUpscalerOutputExtent(renderArea.extent);
                if (canUseUpscalerRenderExtent)
                {
                    const auto providerStatus = provider->status();
                    if (providerStatus.available)
                    {
                        const auto optimalExtent = provider->queryOptimalRenderExtent(renderArea.extent, settings.mode);
                        if (optimalExtent.width > 0u && optimalExtent.height > 0u &&
                            optimalExtent.width <= renderArea.extent.width &&
                            optimalExtent.height <= renderArea.extent.height)
                        {
                            sceneRenderExtent = optimalExtent;
                        }
                    }
                }
            }

            const RenderCamera viewCamera  = cameraForRenderExtent(cam, sceneRenderExtent);
            RenderWorld*       renderWorld = &m_RenderWorldFront;
            if (cam.worldOverride)
            {
                const auto slotIt =
                    std::find_if(m_OverrideRenderWorlds.begin(),
                                 m_OverrideRenderWorlds.end(),
                                 [&](const OverrideRenderWorldSlot& slot) { return slot.world == cam.worldOverride; });
                if (slotIt != m_OverrideRenderWorlds.end())
                    renderWorld = &slotIt->renderWorld;
            }

            RenderView view {
                .renderWorld          = renderWorld,
                .camera               = &viewCamera,
                .target               = target,
                .extent               = sceneRenderExtent,
                .clearValue           = viewCamera.clearValue,
                .stereoMode           = stereoTarget ? StereoRenderMode::eSingleGraphStereo :
                                            (wantsExplicitPerEye ?
                                                 StereoRenderMode::eMono :
                                                 (cam.isXRView ? StereoRenderMode::ePerEyeFallback :
                                                                 StereoRenderMode::eMono)),
                .enableMultiview      = stereoTarget != nullptr,
                .multiviewMask        = stereoTarget ? 0x3u : 0u,
                .multiviewCameras     = {&viewCamera, nullptr},
                .multiviewCameraCount = (stereoTarget || wantsExplicitPerEye) ? 2u : 0u,
                .xrStereoTarget       = canUseXrMultiview ? xrEyeViews[0].stereoTarget : nullptr,
                .xrEyeTargets         = {xrEyeViews.size() > 0u ? xrEyeViews[0].target : nullptr,
                                         xrEyeViews.size() > 1u ? xrEyeViews[1].target : nullptr},
                .gpuSceneDatabase     = renderWorld->gpuSceneDatabase,
                .gpuSceneView         = renderWorld->gpuSceneView,
            };

            if (canUseXrMultiview)
            {
                const auto secondEyeIt = std::find_if(cameraOrder.begin(), cameraOrder.end(), [&](size_t idx) {
                    return cams[idx].isXRView && !cams[idx].isXRPrimaryView && cams[idx].viewCount == cam.viewCount;
                });
                if (secondEyeIt != cameraOrder.end())
                    view.multiviewCameras[1] = &cams[*secondEyeIt];
            }
            if ((stereoTarget || wantsExplicitPerEye) && !view.multiviewCameras[1])
                view.multiviewCameras[1] = &viewCamera;

            rhi::FramebufferInfo fbInfo {
                .area             = renderArea,
                .layers           = stereoTarget ? 2u : 1u,
                .viewMask         = stereoTarget ? 0x3u : 0u,
                .colorAttachments = {rhi::AttachmentInfo {.target = target, .clearValue = viewCamera.clearValue}},
            };

            ViewRenderData viewData {
                .view            = view,
                .framebufferInfo = fbInfo,
            };

            // Fallback clear for camera targets.
            // This guarantees a deterministic background even when renderer contributes no color pass
            // (e.g. pure ImGui examples or empty editor view render targets).
            if (clearedTargetsThisFrame.insert(target).second)
            {
                clearColorTarget(
                    cb, *target, renderArea, viewCamera.clearValue, stereoTarget != nullptr, stereoTarget ? 0x3u : 0u);
            }

            if (!renderer)
            {
                if (static_cast<bool>(target->getUsageFlags() & rhi::ImageUsage::eSampled))
                    rhi::prepareForReading(cb, *target);
                continue;
            }

            const bool useFrameGraph = renderer->usesFrameGraph();

            {
                ImmediateResourceUploader immediateUploader {m_FrameResources, rd};
                prepareCameraData(immediateUploader, viewData, sceneRenderExtent, viewCamera, rd.getBackendApi());
            }

            FrameRenderData  overrideFrameData {};
            FrameRenderData* activeFrameData = &m_PreparedFrameData;
            if (viewCamera.overrideFrameTime)
            {
                ImmediateResourceUploader frameUploader {m_FrameResources, rd};
                prepareFrameData(frameUploader,
                                 overrideFrameData,
                                 m_FrameCounter,
                                 viewCamera.frameTimeSeconds,
                                 viewCamera.frameDeltaSeconds);
                activeFrameData = &overrideFrameData;
            }

            ImmediateRenderContext immediateCtx {
                .cb          = cb,
                .rd          = rd,
                .frame       = *activeFrameData,
                .viewData    = viewData,
                .resourceSet = {},
            };

            rhi::prepareForAttachment(cb, *target, false);
            renderer->render(immediateCtx);
            if (useFrameGraph)
            {
                importPreparedFrameGraphUniforms(fg, *activeFrameData, viewData);
                bb.add<FrameData>(activeFrameData->frameData);
                bb.add<CameraData>(viewData.cameraData);
                bb.add<StereoViewData>(viewData.stereoViewData);
            }

            if (useFrameGraph)
            {
                FrameGraphBuildContext buildCtx {
                    .fg       = fg,
                    .bb       = bb,
                    .rd       = rd,
                    .data     = dataRegistry,
                    .frameResources = &m_FrameResources,
                    .frame    = *activeFrameData,
                    .viewData = viewData,
                };
                const bool captureFrameGraphDebug =
                    m_FrameGraphSnapshotCaptureEnabled || m_FrameGraphTextureCaptureEnabled;

                {
                    RuntimeProfiler::Scope scopeFrameGraphBuild {m_RuntimeProfiler, "FrameGraph::build"};

                    // This sets up the frame graph using a feature renderer or a custom graph-aware renderer.
                    rhi::prepareForAttachment(cb, *target, false);
                    renderer->buildFrameGraph(buildCtx);

                    addFrameGraphTextureCapturePasses(buildCtx, viewCamera);
                    fg.compile();

                    if (captureFrameGraphDebug)
                    {
                        std::ostringstream runtimeDot;
                        fg.debugOutput(runtimeDot, graphviz::Writer {});

                        std::ostringstream       snapshot;
                        FrameGraphSnapshotWriter snapshotWriter {cam.name, cam.rendererKey, runtimeDot.str()};
                        fg.debugOutput(snapshot, snapshotWriter);
                        m_LastFrameGraphSnapshot += snapshot.str();
                        m_LastFrameGraphSnapshot += "\n";
                    }

#ifndef NDEBUG
                    if (captureFrameGraphDebug)
                    {
                        const std::filesystem::path debugRoot = !ctx().config.writableRoot.empty() ?
                                                                    std::filesystem::path(ctx().config.writableRoot) :
                                                                    vbase::executable_dir();
                        const std::filesystem::path debugPath = debugRoot / "framegraph.jsonl";
                        std::ofstream               ofs(debugPath);
                        if (ofs.is_open())
                        {
                            ofs << m_LastFrameGraphSnapshot;
                        }
                        else
                        {
                            VULTRA_CORE_WARN("[RenderSystem] Failed to write framegraph snapshot file: {}",
                                             debugPath.generic_string());
                        }
                    }
#endif
                }

                viewData.framebufferInfo = std::nullopt; // Clear framebuffer info for execution phase, will be set by
                                                         // FrameGraphTexture preRead callback if needed.
                auto* projectShaderLib = shaderService.findProjectLibrary("res://shaders/project.vshaderlib.lua");
                if (!projectShaderLib)
                    projectShaderLib = shaderService.loadProjectLibrary("res://shaders/project.vshaderlib.lua");
                FrameGraphExecContext frameGraphExecCtx {
                    .cb          = cb,
                    .rd          = rd,
                    .frame       = *activeFrameData,
                    .viewData    = viewData,
                    .resourceSet = {},
                    .ext         = {.builtinShaderLib        = &shaderService.builtinLibrary(),
                                    .builtinHighendShaderLib = &shaderService.builtinLibrary(rhi::ShaderProfile::eHighend),
                                    .builtinCompatibilityShaderLib =
                                        &shaderService.builtinLibrary(rhi::ShaderProfile::eCompatibility),
                                    .projectShaderLib = projectShaderLib,
                                    .samplers = m_Samplers},
                };

                {
                    RuntimeProfiler::Scope scopeFrameGraphExec {m_RuntimeProfiler, "FrameGraph::execute"};
                    FG_GPU_ZONE(cb);
                    fg.execute(&frameGraphExecCtx, m_TransientResources.get());
                }
            }

            if (canUseXrMultiview)
                skipRemainingStereoViews = true;

            // Optional ImGui rendering per non-XR camera
            if (imguiService && !cam.isXRView && cam.renderImGui)
            {
                imguiService->begin();
                renderer->onImGui();
                imguiService->end();

                rhi::prepareForAttachment(cb, *target, false);
                imguiService->render(cb, fbInfo);
            }

            if (static_cast<bool>(target->getUsageFlags() & rhi::ImageUsage::eSampled))
                rhi::prepareForReading(cb, *target);
        }

        {
            std::unordered_set<std::string> exposedDebugTextureKeys;
            exposedDebugTextureKeys.reserve(m_FrameGraphDebugTextures.size());
            for (const auto& texture : m_FrameGraphDebugTextures)
            {
                if (texture.texture)
                    exposedDebugTextureKeys.insert(texture.key);
            }

            for (auto it = m_FrameGraphDebugTextureSlots.begin(); it != m_FrameGraphDebugTextureSlots.end();)
            {
                if (exposedDebugTextureKeys.contains(it->first))
                {
                    ++it;
                    continue;
                }

                if (it->second.lastTouchedFrame != m_FrameCounter)
                {
                    it->second.lastTouchedFrame = m_FrameCounter;
                    m_RetiredFrameGraphDebugTextureSlots.push_back(std::move(it->second));
                    it = m_FrameGraphDebugTextureSlots.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        if (imguiService && backendService.isXREnabled() && backendService.isXRMirrorEnabled() && !xrEyeViews.empty())
        {
            struct alignas(16) MirrorPreviewPushConstants
            {
                glm::ivec4 channelMask {1, 1, 1, 1};
                int        gammaCorrect {0};
                int        previewMode {0};
                float      depthNear {0.1f};
                float      depthFar {1000.0f};
                float      clampMin {0.0f};
                float      clampMax {1.0f};
            };

            for (const auto& eyeView : xrEyeViews)
            {
                if (!eyeView.target || !eyeView.mirrorTarget)
                    continue;

                if (eyeView.stereoTarget)
                {
                    eyeView.target->setBarrierState(eyeView.stereoTarget->getLastBarrierScope(),
                                                    eyeView.stereoTarget->getImageLayout());
                }

                rhi::prepareForReading(cb, *eyeView.target);
                if (m_BuiltinRenderSettings.xrMirrorGammaCorrect)
                {
                    auto* pipeline =
                        getFrameGraphTexturePreviewPipeline(rd,
                                                            shaderService.builtinLibrary(rhi::ShaderProfile::eGeneral),
                                                            eyeView.mirrorTarget->getPixelFormat());
                    const auto samplerIt = m_Samplers.find("linear");
                    if (pipeline && samplerIt != m_Samplers.end())
                    {
                        const auto descriptorSet = cb.createDescriptorSetBuilder()
                                                       .bind(0,
                                                             rhi::bindings::CombinedImageSampler {
                                                                 .texture     = eyeView.target,
                                                                 .imageAspect = rhi::ImageAspect::eColor,
                                                                 .sampler     = samplerIt->second,
                                                             })
                                                       .build(pipeline->getDescriptorSetLayout(3));
                        const MirrorPreviewPushConstants pc {
                            .channelMask  = glm::ivec4(1, 1, 1, 0),
                            .gammaCorrect = 1,
                            .previewMode  = 0,
                            .depthNear    = 0.1f,
                            .depthFar     = 1000.0f,
                            .clampMin     = 0.0f,
                            .clampMax     = 1.0f,
                        };
                        rhi::prepareForAttachment(cb, *eyeView.mirrorTarget, false);
                        cb.bindPipeline(*pipeline)
                            .bindDescriptorSet(3, descriptorSet)
                            .pushConstants(rhi::ShaderStages::eFragment, 0, &pc)
                            .beginRendering({
                                .area             = {.extent = eyeView.mirrorTarget->getExtent()},
                                .colorAttachments = {rhi::AttachmentInfo {.target = eyeView.mirrorTarget}},
                            })
                            .drawFullScreenTriangle()
                            .endRendering();
                    }
                    else
                    {
                        cb.blit(*eyeView.target, *eyeView.mirrorTarget, rhi::TexelFilter::eLinear);
                    }
                }
                else
                {
                    cb.blit(*eyeView.target, *eyeView.mirrorTarget, rhi::TexelFilter::eLinear);
                }
                rhi::prepareForReading(cb, *eyeView.target);

                if (eyeView.stereoTarget)
                {
                    eyeView.stereoTarget->setBarrierState(eyeView.target->getLastBarrierScope(),
                                                          eyeView.target->getImageLayout());
                }
            }

            imguiService->begin();

            std::unordered_set<Renderer*> imguiRenderers;
            for (const size_t cameraIdx : cameraOrder)
            {
                const auto& cam = cams[cameraIdx];
                if (!cam.renderImGui)
                    continue;

                auto renderer = resolveRenderer(cam);
                if (!renderer || imguiRenderers.contains(renderer.get()))
                    continue;
                imguiRenderers.insert(renderer.get());
                renderer->onImGui();
            }

            imguiService->end();

            for (const auto& xrEyeView : xrEyeViews)
            {
                if (xrEyeView.mirrorTarget)
                    rhi::prepareForReading(cb, *xrEyeView.mirrorTarget);
            }

            const rhi::Rect2D imguiArea = window.platformType() == os::Window::PlatformType::eAndroidNativeWindow ?
                                              window.getContentArea() :
                                              rhi::Rect2D {.offset = {0, 0}, .extent = defaultTarget.getExtent()};

            rhi::FramebufferInfo imguiFbInfo {
                .area             = imguiArea,
                .colorAttachments = {rhi::AttachmentInfo {.target = &defaultTarget}},
            };

            rhi::prepareForAttachment(cb, defaultTarget, false);
            imguiService->render(cb, imguiFbInfo);
        }

        // Stop issuing begin/end scope queries after rendering submission building is done,
        // but keep resolve callback alive so endFrame can harvest ready GPU samples.
        if (gpuTimingEnabled)
        {
            m_RuntimeProfiler.setGpuScopeCallbacks([]() { return uint64_t {0}; },
                                                   [](const uint64_t) {},
                                                   [&rd](const uint64_t token) { return rd.consumeScopeGpuMs(token); });
        }

        m_TransientResources->update();
        if (gpuTimingEnabled)
        {
            rd.endFrameGpuQuery(cb);
        }
        backendService.endFrame();

        const auto commandStats = rhi::CommandBuffer::consumeFrameStats();
        m_RuntimeProfiler.setCommandStats(commandStats.drawCalls,
                                          commandStats.dispatchCalls,
                                          commandStats.traceRaysCalls,
                                          commandStats.copyOps,
                                          commandStats.updateOps);
        const auto assetMemoryStats = assetService.memoryStats();
        const auto memoryStats      = rd.getMemoryStats();
        m_RuntimeProfiler.setMemoryStats(assetMemoryStats.cpuCacheBytes,
                                         memoryStats.cpuCacheBytes,
                                         memoryStats.gpuDeviceLocalBytes,
                                         memoryStats.gpuHostVisibleBytes);
        const double gpuFrameMs = gpuTimingEnabled ? rd.consumeGpuFrameMs() : -1.0;
        m_RuntimeProfiler.setGpuFrameMs(gpuFrameMs);
        const auto renderFrameCpuEnd = std::chrono::steady_clock::now();
        m_RuntimeProfiler.setCpuRenderMs(
            std::chrono::duration<double, std::milli>(renderFrameCpuEnd - renderFrameCpuStart).count());
        m_RuntimeProfiler.endFrame();
        if (m_FrameGraphTextureDumpCaptureFrames > 0)
            --m_FrameGraphTextureDumpCaptureFrames;
        updateGaussianSplatFoveatedBudgetController(m_GaussianSplatSettings, gpuFrameMs);
        rhi::setBuiltinProfilerGpuScopeCallbacks({}, {});
        m_RuntimeProfiler.setGpuScopeCallbacks({}, {}, {});
    }

    // All debug-draw submission forwards to the global dd:: immediate-mode queue, which the builtin
    // DebugDraw render pass flushes (and clears) each frame. Geometry is world space; duration 0.
    void RenderSystem::debugDrawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color)
    {
        dd::line(glm::value_ptr(from), glm::value_ptr(to), glm::value_ptr(color));
    }

    void RenderSystem::debugDrawAabb(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color)
    {
        dd::aabb(glm::value_ptr(min), glm::value_ptr(max), glm::value_ptr(color));
    }

    void RenderSystem::debugDrawBox(const glm::mat4& worldMatrix, const glm::vec3& halfExtents, const glm::vec3& color)
    {
        // Oriented box: transform the 8 local corners and draw the 12 edges (dd::box's point order is
        // fixed, so emit explicit edges to stay correct under rotation/scale).
        glm::vec3 c[8];
        int       i = 0;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
                for (int sz = -1; sz <= 1; sz += 2)
                    c[i++] = glm::vec3(worldMatrix * glm::vec4(static_cast<float>(sx) * halfExtents.x,
                                                               static_cast<float>(sy) * halfExtents.y,
                                                               static_cast<float>(sz) * halfExtents.z,
                                                               1.0f));
        // Index pattern matches the (sx, sy, sz) iteration order above.
        constexpr int edges[12][2] = {
            {0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
            {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };
        for (const auto& e : edges)
            dd::line(glm::value_ptr(c[e[0]]), glm::value_ptr(c[e[1]]), glm::value_ptr(color));
    }

    void RenderSystem::debugDrawSphere(const glm::vec3& center, float radius, const glm::vec3& color)
    {
        dd::sphere(glm::value_ptr(center), glm::value_ptr(color), radius);
    }

    void RenderSystem::debugDrawFrustum(const glm::mat4& invViewProjection, const glm::vec3& color)
    {
        dd::frustum(glm::value_ptr(invViewProjection), glm::value_ptr(color));
    }

    void RenderSystem::onPreRender() { m_SkipRender = false; }

    void RenderSystem::onRender() { renderFrame(); }

    void RenderSystem::onPostRender()
    {
        auto* imguiService = ctx().services.tryGet<IImGuiService>();
        if (imguiService)
            imguiService->postRender();
    }

    void RenderSystem::onPresent()
    {
        if (m_SkipRender)
            return;

        auto& backendService = ctx().services.require<IRenderBackendService>();
        backendService.present();

        if (auto* frameDebuggerService = ctx().services.tryGet<IFrameDebuggerService>())
            frameDebuggerService->captureEnd();
    }
} // namespace vultra
