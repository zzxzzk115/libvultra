#pragma once

#include "vultra/core/base/uuid.hpp"

#include <glm/glm.hpp>

#include <string>

namespace vultra
{
    struct CanvasComponent
    {
        bool      enabled {true};
        int       sortOrder {0};
        glm::vec2 referenceResolutionPx {1920.0f, 1080.0f};
        uint32_t  scaleMode {1};  // 0: constant pixel size, 1: scale with screen.
        uint32_t  renderMode {0}; // 0: screen overlay, 1: world space (3D, follows entity transform).
        // World-space only: canvas pixels per world unit (meter). Larger = smaller canvas.
        float pixelsPerUnit {250.0f};
    };

    struct RectTransformComponent
    {
        glm::vec2 anchorMin {0.5f, 0.5f};
        glm::vec2 anchorMax {0.5f, 0.5f};
        glm::vec2 pivot {0.5f, 0.5f};
        glm::vec2 anchoredPositionPx {0.0f, 0.0f};
        glm::vec2 sizeDeltaPx {100.0f, 100.0f};
        float     rotation {0.0f};
        glm::vec2 scale {1.0f, 1.0f};
    };

    struct UiPanelComponent
    {
        bool      enabled {true};
        glm::vec4 color {0.12f, 0.14f, 0.18f, 0.92f};
        float     borderRadiusPx {0.0f};
    };

    struct UiImageComponent
    {
        bool      enabled {true};
        CoreUUID  texture;
        glm::vec4 tint {1.0f};
        uint32_t  fitMode {0}; // 0: stretch, 1: contain, 2: cover.
    };

    struct UiTextComponent
    {
        bool        enabled {true};
        std::string text {"Text"};
        glm::vec4   color {1.0f};
        float       fontSizePx {24.0f};
        uint32_t    horizontalAlign {0}; // 0: left, 1: center, 2: right.
        uint32_t    verticalAlign {1};   // 0: top, 1: middle, 2: bottom.
        // Font asset: a project font (eFont) UUID or a builtin font (builtinFontUuidForUri,
        // e.g. "builtin://fonts/noto_sans_cjk.otf"). Empty = default builtin font.
        CoreUUID    font;
    };

    struct UiButtonComponent
    {
        bool      enabled {true};
        bool      interactable {true};
        CoreUUID  targetGraphic;
        glm::vec4 normalColor {0.18f, 0.22f, 0.28f, 1.0f};
        glm::vec4 hoveredColor {0.24f, 0.30f, 0.38f, 1.0f};
        glm::vec4 pressedColor {0.12f, 0.16f, 0.22f, 1.0f};
        bool      hovered {false};
        bool      pressed {false};
        bool      clicked {false};
    };

    struct UiToggleComponent
    {
        bool      enabled {true};
        bool      interactable {true};
        bool      checked {false};
        glm::vec4 offColor {0.18f, 0.22f, 0.28f, 1.0f};
        glm::vec4 onColor {0.16f, 0.48f, 0.84f, 1.0f};
        glm::vec4 checkColor {1.0f};
    };

    struct UiSliderComponent
    {
        bool      enabled {true};
        bool      interactable {true};
        float     value {0.5f};
        float     minValue {0.0f};
        float     maxValue {1.0f};
        glm::vec4 trackColor {0.16f, 0.18f, 0.22f, 1.0f};
        glm::vec4 fillColor {0.18f, 0.50f, 0.88f, 1.0f};
        glm::vec4 handleColor {0.95f, 0.97f, 1.0f, 1.0f};
    };

    struct UiProgressBarComponent
    {
        bool      enabled {true};
        float     value {0.5f};
        float     minValue {0.0f};
        float     maxValue {1.0f};
        glm::vec4 trackColor {0.14f, 0.16f, 0.20f, 1.0f};
        glm::vec4 fillColor {0.20f, 0.62f, 0.34f, 1.0f};
    };

    struct UiLayoutComponent
    {
        bool      enabled {true};
        uint32_t  kind {0}; // 0: none, 1: horizontal, 2: vertical, 3: grid.
        glm::vec4 paddingPx {0.0f}; // left, top, right, bottom.
        glm::vec4 marginPx {0.0f};  // left, top, right, bottom.
        float     spacingPx {0.0f};
        glm::vec2 cellSizePx {100.0f, 100.0f};
    };
} // namespace vultra
