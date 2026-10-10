#pragma once

#include <vultra/drivers/openxr/openxr.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace vultra
{
    struct EyeProfile
    {
        Extent    size;
        glm::vec4 tangents; // Signed left, right, down, up in eye-local coordinates.
        glm::mat4 pose {1}; // Eye-to-head, including position and orientation.
    };

    struct HeadsetProfile
    {
        std::string               name;
        std::string               source;
        std::string               runtime;
        std::array<EyeProfile, 2> eyes;

        void                  validate() const;
        std::string           serialize() const;
        static HeadsetProfile parse(std::string_view text);
        void                  save(const std::filesystem::path& file) const;
        static HeadsetProfile load(const std::filesystem::path& file);
        static HeadsetProfile capture(const OpenXRSystem& system, const XRFrame& frame);
        void updateEyes(const XRFrame& frame); // Refresh geometry without querying immutable runtime identity.
    };

    // Runtime measurements supplied with the original research data; IPD is a captured user setting.
    const std::array<HeadsetProfile, 3>& measuredHeadsetProfiles();
    glm::mat4                            eyeProjection(const EyeProfile& eye, float nearPlane, float farPlane);
} // namespace vultra
