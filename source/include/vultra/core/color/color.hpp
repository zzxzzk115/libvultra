#pragma once

#include <glm/glm.hpp>

namespace vultra
{
    namespace color
    {
        inline glm::vec3 sRGBToLinear(const glm::vec3& srgb) { return glm::pow(srgb, glm::vec3(2.2f)); }

        inline glm::vec4 sRGBToLinear(const glm::vec4& srgb)
        {
            return glm::vec4(glm::pow(glm::vec3(srgb), glm::vec3(2.2f)), srgb.a);
        }

        inline glm::vec3 linearTosRGB(const glm::vec3& linear) { return glm::pow(linear, glm::vec3(1.0f / 2.2f)); }

        inline glm::vec4 linearTosRGB(const glm::vec4& linear)
        {
            return glm::vec4(glm::pow(glm::vec3(linear), glm::vec3(1.0f / 2.2f)), linear.a);
        }
    } // namespace color
} // namespace vultra