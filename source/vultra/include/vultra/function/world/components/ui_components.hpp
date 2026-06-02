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
        uint32_t  scaleMode {1}; // 0: constant pixel size, 1: scale with screen.
    };

    struct RectTransformComponent
    {
        glm::vec2 anchorMin {0.5f, 0.5f};
        glm::vec2 anchorMax {0.5f, 0.5f};
        glm::vec2 pivot {0.5f, 0.5f};
        glm::vec2 anchoredPositionPx {0.0f, 0.0f};
        glm::vec2 sizeDeltaPx {100.0f, 100.0f};
        float     rotationDegrees {0.0f};
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
    };

    struct UiButtonComponent
    {
        bool      enabled {true};
        bool      interactable {true};
        glm::vec4 normalColor {0.18f, 0.22f, 0.28f, 1.0f};
        glm::vec4 hoveredColor {0.24f, 0.30f, 0.38f, 1.0f};
        glm::vec4 pressedColor {0.12f, 0.16f, 0.22f, 1.0f};
        bool      hovered {false};
        bool      pressed {false};
        bool      clicked {false};
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
