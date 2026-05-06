#pragma once

#include "vultra/core/os/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vultra::platform
{
    struct CursorImage
    {
        int                  width {64};
        int                  height {64};
        int                  hotX {32};
        int                  hotY {32};
        std::vector<uint8_t> pixels {};
    };

    namespace detail
    {
        struct Color
        {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t a;
        };

        inline constexpr Color kTransparent {0, 0, 0, 0};
        inline constexpr Color kPanelShadow {6, 10, 8, 180};
        inline constexpr Color kAccent {45, 255, 100, 255};
        inline constexpr Color kAccentSoft {220, 255, 232, 255};

        inline void putPixel(CursorImage& image, int x, int y, Color color)
        {
            if (x < 0 || y < 0 || x >= image.width || y >= image.height || color.a == 0)
                return;

            const int index = (y * image.width + x) * 4;
            image.pixels[index + 0] = color.r;
            image.pixels[index + 1] = color.g;
            image.pixels[index + 2] = color.b;
            image.pixels[index + 3] = color.a;
        }

        inline void initImage(CursorImage& image)
        {
            image.width = 64;
            image.height = 64;
            image.pixels.assign(static_cast<size_t>(image.width * image.height * 4), 0);
        }

        inline bool insideRoundedRect(int x, int y, float left, float top, float right, float bottom, float radius)
        {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float clampedX = std::clamp(px, left + radius, right - radius);
            const float clampedY = std::clamp(py, top + radius, bottom - radius);
            const float dx = px - clampedX;
            const float dy = py - clampedY;
            return (dx * dx + dy * dy) <= radius * radius;
        }

        inline void drawRoundedRectFill(CursorImage& image,
                                        float       left,
                                        float       top,
                                        float       right,
                                        float       bottom,
                                        float       radius,
                                        Color       color)
        {
            for (int y = 0; y < image.height; ++y)
            {
                for (int x = 0; x < image.width; ++x)
                {
                    if (insideRoundedRect(x, y, left, top, right, bottom, radius))
                        putPixel(image, x, y, color);
                }
            }
        }

        inline void drawRoundedRectStroke(CursorImage& image,
                                          float       left,
                                          float       top,
                                          float       right,
                                          float       bottom,
                                          float       radius,
                                          float       thickness,
                                          Color       color)
        {
            for (int y = 0; y < image.height; ++y)
            {
                for (int x = 0; x < image.width; ++x)
                {
                    const bool outer = insideRoundedRect(x, y, left, top, right, bottom, radius);
                    const bool inner = insideRoundedRect(
                        x, y, left + thickness, top + thickness, right - thickness, bottom - thickness, radius);
                    if (outer && !inner)
                        putPixel(image, x, y, color);
                }
            }
        }

        inline void drawLine(CursorImage& image,
                             float       x0,
                             float       y0,
                             float       x1,
                             float       y1,
                             float       thickness,
                             Color       color)
        {
            const float dx = x1 - x0;
            const float dy = y1 - y0;
            const float len2 = dx * dx + dy * dy;
            if (len2 <= 0.0001f)
                return;

            for (int y = 0; y < image.height; ++y)
            {
                for (int x = 0; x < image.width; ++x)
                {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    const float t = std::clamp(((px - x0) * dx + (py - y0) * dy) / len2, 0.0f, 1.0f);
                    const float cx = x0 + t * dx;
                    const float cy = y0 + t * dy;
                    const float ddx = px - cx;
                    const float ddy = py - cy;
                    if ((ddx * ddx + ddy * ddy) <= thickness * thickness)
                        putPixel(image, x, y, color);
                }
            }
        }

        inline void drawCircleStroke(
            CursorImage& image, float cx, float cy, float radius, float thickness, Color color, float startAngle = 0.0f, float endAngle = 6.28318f)
        {
            for (int y = 0; y < image.height; ++y)
            {
                for (int x = 0; x < image.width; ++x)
                {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    const float dx = px - cx;
                    const float dy = py - cy;
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    float angle = std::atan2(dy, dx);
                    if (angle < 0.0f)
                        angle += 6.28318f;

                    const bool withinArc = startAngle <= endAngle ? (angle >= startAngle && angle <= endAngle) :
                                                                     (angle >= startAngle || angle <= endAngle);
                    if (withinArc && std::abs(dist - radius) <= thickness)
                        putPixel(image, x, y, color);
                }
            }
        }

        inline void drawCircleFill(CursorImage& image, float cx, float cy, float radius, Color color)
        {
            for (int y = 0; y < image.height; ++y)
            {
                for (int x = 0; x < image.width; ++x)
                {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    const float dx = px - cx;
                    const float dy = py - cy;
                    if ((dx * dx + dy * dy) <= radius * radius)
                        putPixel(image, x, y, color);
                }
            }
        }

        inline void drawEllipseStroke(CursorImage& image, float cx, float cy, float rx, float ry, float thickness, Color color)
        {
            for (int y = 0; y < image.height; ++y)
            {
                for (int x = 0; x < image.width; ++x)
                {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    const float nx = (px - cx) / rx;
                    const float ny = (py - cy) / ry;
                    const float outer = nx * nx + ny * ny;
                    const float inx = (px - cx) / std::max(0.001f, rx - thickness);
                    const float iny = (py - cy) / std::max(0.001f, ry - thickness);
                    const float inner = inx * inx + iny * iny;
                    if (outer <= 1.0f && inner >= 1.0f)
                        putPixel(image, x, y, color);
                }
            }
        }

        inline CursorImage makeOrbitCursor()
        {
            constexpr float s = 2.0f;
            CursorImage image {};
            initImage(image);
            image.hotX = 32;
            image.hotY = 32;

            drawCircleStroke(image, 16.0f * s, 16.0f * s, 5.9f * s, 1.8f * s, kPanelShadow, 0.55f, 5.45f);
            drawCircleStroke(image, 16.0f * s, 16.0f * s, 5.6f * s, 1.0f * s, kAccentSoft, 0.55f, 5.45f);
            drawLine(image, 20.4f * s, 8.8f * s, 24.4f * s, 9.9f * s, 1.6f * s, kPanelShadow);
            drawLine(image, 24.4f * s, 9.9f * s, 21.4f * s, 13.1f * s, 1.6f * s, kPanelShadow);
            drawLine(image, 20.2f * s, 8.8f * s, 24.2f * s, 10.0f * s, 0.9f * s, kAccentSoft);
            drawLine(image, 24.2f * s, 10.0f * s, 21.4f * s, 13.0f * s, 0.9f * s, kAccentSoft);
            drawCircleFill(image, 16.0f * s, 16.0f * s, 1.5f * s, kAccent);
            return image;
        }

        inline CursorImage makeGrabCursor()
        {
            constexpr float s = 2.0f;
            CursorImage image {};
            initImage(image);
            image.hotX = 32;
            image.hotY = 32;

            drawLine(image, 11.5f * s, 23.0f * s, 11.5f * s, 14.5f * s, 1.5f * s, kPanelShadow);
            drawLine(image, 14.0f * s, 22.0f * s, 14.0f * s, 11.8f * s, 1.5f * s, kPanelShadow);
            drawLine(image, 16.5f * s, 22.0f * s, 16.5f * s, 13.3f * s, 1.5f * s, kPanelShadow);
            drawLine(image, 19.0f * s, 21.5f * s, 19.0f * s, 14.0f * s, 1.5f * s, kPanelShadow);
            drawLine(image, 21.5f * s, 20.8f * s, 21.5f * s, 16.2f * s, 1.5f * s, kPanelShadow);
            drawLine(image, 11.5f * s, 23.0f * s, 13.5f * s, 25.2f * s, 1.5f * s, kPanelShadow);
            drawLine(image, 13.5f * s, 25.2f * s, 19.2f * s, 25.2f * s, 1.5f * s, kPanelShadow);
            drawLine(image, 19.2f * s, 25.2f * s, 22.3f * s, 22.6f * s, 1.5f * s, kPanelShadow);
            drawLine(image, 22.3f * s, 22.6f * s, 22.3f * s, 18.0f * s, 1.5f * s, kPanelShadow);

            drawLine(image, 11.5f * s, 23.0f * s, 11.5f * s, 14.5f * s, 0.9f * s, kAccentSoft);
            drawLine(image, 14.0f * s, 22.0f * s, 14.0f * s, 11.8f * s, 0.9f * s, kAccentSoft);
            drawLine(image, 16.5f * s, 22.0f * s, 16.5f * s, 13.3f * s, 0.9f * s, kAccentSoft);
            drawLine(image, 19.0f * s, 21.5f * s, 19.0f * s, 14.0f * s, 0.9f * s, kAccentSoft);
            drawLine(image, 21.5f * s, 20.8f * s, 21.5f * s, 16.2f * s, 0.9f * s, kAccentSoft);
            drawLine(image, 11.5f * s, 23.0f * s, 13.5f * s, 25.2f * s, 0.9f * s, kAccentSoft);
            drawLine(image, 13.5f * s, 25.2f * s, 19.2f * s, 25.2f * s, 0.9f * s, kAccentSoft);
            drawLine(image, 19.2f * s, 25.2f * s, 22.3f * s, 22.6f * s, 0.9f * s, kAccentSoft);
            drawLine(image, 22.3f * s, 22.6f * s, 22.3f * s, 18.0f * s, 0.9f * s, kAccentSoft);
            drawLine(image, 12.0f * s, 24.0f * s, 20.1f * s, 24.0f * s, 0.7f * s, kAccent);
            return image;
        }

        inline CursorImage makeLookCursor()
        {
            constexpr float s = 2.0f;
            CursorImage image {};
            initImage(image);
            image.hotX = 32;
            image.hotY = 32;

            drawEllipseStroke(image, 16.0f * s, 16.0f * s, 8.3f * s, 5.0f * s, 1.8f * s, kPanelShadow);
            drawCircleStroke(image, 16.0f * s, 16.0f * s, 2.8f * s, 1.6f * s, kPanelShadow);
            drawEllipseStroke(image, 16.0f * s, 16.0f * s, 8.0f * s, 4.7f * s, 1.0f * s, kAccentSoft);
            drawCircleStroke(image, 16.0f * s, 16.0f * s, 2.5f * s, 0.9f * s, kAccentSoft);
            drawCircleFill(image, 16.0f * s, 16.0f * s, 1.0f * s, kAccent);
            return image;
        }
    } // namespace detail

    inline CursorImage makeCursorImage(os::Window::CursorType type)
    {
        switch (type)
        {
            case os::Window::CursorType::eOrbit:
                return detail::makeOrbitCursor();
            case os::Window::CursorType::eGrab:
                return detail::makeGrabCursor();
            case os::Window::CursorType::eLook:
                return detail::makeLookCursor();
            case os::Window::CursorType::eZoomIn:
            case os::Window::CursorType::eZoomOut:
            case os::Window::CursorType::eArrow:
            default:
                return CursorImage {};
        }
    }
} // namespace vultra::platform
