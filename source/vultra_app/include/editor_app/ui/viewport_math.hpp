#pragma once

#include <imgui.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <optional>

// Small screen<->world helpers for the scene viewport.
//
// Conventions match ImGuizmo (which is driven by the same view/projection matrices and is known to
// map correctly): clip space comes from glm::perspectiveRH_ZO (depth 0..1), and the framebuffer Y
// axis is flipped relative to NDC Y. `imagePos`/`avail` are the ImGui::Image screen rect
// (GetCursorScreenPos + GetContentRegionAvail) the scene texture is drawn into.
namespace vultra_app::viewport
{
    // Projects a world-space point to absolute screen coordinates. Returns nullopt when the point is
    // at/behind the camera plane (clip.w <= 0), where the projection is undefined.
    inline std::optional<ImVec2>
    worldToScreen(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& worldPos, const ImVec2& imagePos, const ImVec2& avail)
    {
        const glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 1e-6f)
            return std::nullopt;

        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;

        const float sx = ndcX * 0.5f + 0.5f;
        const float sy = 0.5f - ndcY * 0.5f; // flip Y

        return ImVec2 {imagePos.x + sx * avail.x, imagePos.y + sy * avail.y};
    }

    struct Ray
    {
        glm::vec3 origin {0.0f};
        glm::vec3 dir {0.0f, 0.0f, -1.0f};
    };

    // Builds a world-space ray from an absolute screen position (e.g. the mouse).
    inline Ray
    screenToWorldRay(const glm::mat4& view, const glm::mat4& proj, const ImVec2& screen, const ImVec2& imagePos, const ImVec2& avail)
    {
        const float u = avail.x > 0.0f ? (screen.x - imagePos.x) / avail.x : 0.0f;
        const float v = avail.y > 0.0f ? (screen.y - imagePos.y) / avail.y : 0.0f;

        const float ndcX = u * 2.0f - 1.0f;
        const float ndcY = (1.0f - v) * 2.0f - 1.0f; // flip Y back

        const glm::mat4 invVp = glm::inverse(proj * view);

        glm::vec4 nearH = invVp * glm::vec4(ndcX, ndcY, 0.0f, 1.0f); // ZO near plane z=0
        glm::vec4 farH  = invVp * glm::vec4(ndcX, ndcY, 1.0f, 1.0f); // ZO far plane  z=1

        const glm::vec3 nearP = glm::vec3(nearH) / nearH.w;
        const glm::vec3 farP  = glm::vec3(farH) / farH.w;

        Ray ray;
        ray.origin = nearP;
        ray.dir    = glm::normalize(farP - nearP);
        return ray;
    }

    // Intersects the ray with the horizontal plane y == planeY. Returns the ray parameter t (>= 0) of
    // the hit, or nullopt when the ray is parallel to / pointing away from the plane.
    inline std::optional<float> rayPlaneY(const Ray& ray, float planeY)
    {
        if (std::abs(ray.dir.y) < 1e-6f)
            return std::nullopt;
        const float t = (planeY - ray.origin.y) / ray.dir.y;
        if (t < 0.0f)
            return std::nullopt;
        return t;
    }
} // namespace vultra_app::viewport
