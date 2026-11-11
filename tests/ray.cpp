#include "support/check.hpp"

#include <vultra/core/math/ray.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <iostream>

namespace
{
    using support::must;
    using support::require;

    bool nearly(float a, float b)
    {
        return std::abs(a - b) < 1e-5f;
    }
} // namespace

int main()
{
    using namespace vultra;
    const Ray  forward {{0, 0, 2}, {0, 0, -1}};
    const auto plane = intersectPlane(forward, {0, 0, 0}, {0, 0, 1});
    require(plane && nearly(*plane, 2), "Ray missed a facing plane");
    require(intersectPlane(forward, {0, 0, 0}, {0, 0, -1}).has_value(), "Planes must be two-sided");
    require(!intersectPlane(forward, {0, 0, 3}, {0, 0, 1}), "Ray hit a plane behind its origin");
    require(!intersectPlane({{0, 0, 2}, {1, 0, 0}}, {0, 0, 0}, {0, 0, 1}), "A parallel ray hit a plane");

    // Distances are in units of the direction, so a doubled direction halves them.
    const auto scaled = intersectPlane({{0, 0, 2}, {0, 0, -2}}, {0, 0, 0}, {0, 0, 1});
    require(scaled && nearly(*scaled, 1), "Distance is not in units of the direction");

    const auto triangle = intersectTriangle({{0.25f, 0.5f, 1}, {0, 0, -1}}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0});
    require(triangle && nearly(triangle->distance, 1) && nearly(triangle->barycentric.x, 0.25f) &&
                nearly(triangle->barycentric.y, 0.5f),
            "Triangle hit or barycentrics are wrong");
    require(intersectTriangle({{0.25f, 0.5f, -1}, {0, 0, 1}}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}).has_value(),
            "Triangles must be two-sided");
    require(!intersectTriangle({{0.75f, 0.75f, 1}, {0, 0, -1}}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}),
            "Ray hit outside the triangle");

    // A panel two units wide and one high, facing +Z, hit a quarter of the way across and three quarters up.
    const auto panel = intersectRectangle({{-0.5f, 0.75f, 3}, {0, 0, -1}}, {-1, 0, 0}, {2, 0, 0}, {0, 1, 0});
    require(panel && nearly(panel->distance, 3) && nearly(panel->uv.x, 0.25f) && nearly(panel->uv.y, 0.75f),
            "Rectangle hit or its coordinates are wrong");
    require(!intersectRectangle({{1.5f, 0.5f, 3}, {0, 0, -1}}, {-1, 0, 0}, {2, 0, 0}, {0, 1, 0}),
            "Ray hit outside the rectangle");
    const auto skewed = intersectRectangle({{1.0f, 0.5f, 3}, {0, 0, -1}}, {0, 0, 0}, {1, 0, 0}, {1, 1, 0});
    require(skewed && nearly(skewed->uv.x, 0.5f) && nearly(skewed->uv.y, 0.5f), "Skewed rectangle coordinates are wrong");

    const RenderCamera camera {glm::lookAtRH(glm::vec3(0, 0, 5), glm::vec3(0), glm::vec3(0, 1, 0)),
                               glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 0.5f, 50.0f),
                               0.5f,
                               50};
    const auto         center = cameraRay(camera, {0, 0});
    require(nearly(center.origin.z, 4.5f) && nearly(center.direction.z, -1) && nearly(center.direction.x, 0),
            "Centre camera ray does not start on the near plane looking forward");
    // With a 90 degree field of view the image edge is 45 degrees off axis.
    const auto edge = cameraRay(camera, {1, 0});
    require(nearly(edge.direction.x, -edge.direction.z) && edge.direction.x > 0, "Edge camera ray has the wrong angle");
    const auto picked = intersectRectangle(cameraRay(camera, {0.2f, -0.1f}), {-5, -5, 0}, {10, 0, 0}, {0, 10, 0});
    require(picked && nearly(picked->uv.x, 0.6f) && nearly(picked->uv.y, 0.45f), "Camera ray picked the wrong panel point");
    std::cout << "Ray tests passed: plane, triangle, rectangle and camera rays\n";
    return 0;
}
