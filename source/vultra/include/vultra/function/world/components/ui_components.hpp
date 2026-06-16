#pragma once

#include "vultra/core/base/uuid.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

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
        // Optional i18n catalog key. When non-empty, the rendered string is tr(localizationKey)
        // for the active language; otherwise the literal `text` is used.
        std::string localizationKey;
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

    // Single-line editable text field. The UiSystem owns focus + editing (typed text via the
    // text-input stream, Backspace/arrows/Enter); text overflow is clipped to the rect.
    struct UiInputFieldComponent
    {
        bool        enabled {true};
        bool        interactable {true};
        std::string text;
        std::string placeholder {"Enter text..."};
        uint32_t    maxLength {256};
        float       fontSizePx {20.0f};
        CoreUUID    font; // empty = default builtin font
        glm::vec4   normalColor {0.10f, 0.12f, 0.16f, 1.0f};
        glm::vec4   focusedColor {0.14f, 0.17f, 0.22f, 1.0f};
        glm::vec4   textColor {0.95f, 0.97f, 1.0f, 1.0f};
        glm::vec4   placeholderColor {0.55f, 0.58f, 0.64f, 1.0f};
        glm::vec4   caretColor {0.95f, 0.97f, 1.0f, 1.0f};
        int         caret {0};       // caret index into text (runtime)
        bool        focused {false}; // read-only: set by UiSystem
        bool        submitted {false}; // read-only: true the frame Enter is pressed
    };

    // 1D dropdown selector. The header shows the selected option; clicking expands an immediate
    // option list (rendered by the render system, hit-tested by the UiSystem).
    struct UiDropdownComponent
    {
        bool                     enabled {true};
        bool                     interactable {true};
        std::vector<std::string> options;
        int                      selectedIndex {0};
        float                    fontSizePx {18.0f};
        CoreUUID                 font;
        glm::vec4                normalColor {0.16f, 0.19f, 0.24f, 1.0f};
        glm::vec4                hoveredColor {0.22f, 0.27f, 0.34f, 1.0f};
        glm::vec4                panelColor {0.10f, 0.12f, 0.16f, 0.98f};
        glm::vec4                selectedColor {0.18f, 0.42f, 0.72f, 1.0f};
        glm::vec4                textColor {0.95f, 0.97f, 1.0f, 1.0f};
        bool                     expanded {false}; // read-only: set by UiSystem
    };

    // Clipped, scrollable viewport. The UiSystem scrolls on wheel/drag over the view and offsets
    // its descendants; the render system clips descendant draws to the view bounds.
    struct UiScrollViewComponent
    {
        bool      enabled {true};
        glm::vec2 contentSizePx {0.0f, 0.0f}; // 0 on an axis = no scrolling on that axis
        glm::vec2 scrollPx {0.0f, 0.0f};      // runtime scroll offset
        bool      horizontal {false};
        bool      vertical {true};
        float     scrollSpeedPx {40.0f};
        glm::vec4 backgroundColor {0.08f, 0.09f, 0.12f, 0.6f};
    };
} // namespace vultra
