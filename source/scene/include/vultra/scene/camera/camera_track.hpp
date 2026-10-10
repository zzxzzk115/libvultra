#pragma once

#include <vultra/core/math/extent.hpp>
#include <vultra/core/math/render_camera.hpp>

#include <glm/gtc/quaternion.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace vultra
{
    struct CameraPose
    {
        glm::vec3 position {0};
        glm::quat orientation {1, 0, 0, 0}; // Camera-to-world; local forward is -Z.
        float     verticalFov = glm::radians(60.0f);
        float     nearPlane   = 0.05f;
        float     farPlane    = 1000;

        void              validate() const;
        RenderCamera      camera(Extent size) const;
        static CameraPose fromCamera(const RenderCamera& camera);
    };

    struct CameraKeyframe
    {
        uint64_t   frame = 0;
        CameraPose pose;
    };

    // Frame indices, rather than wall-clock time, define reproducible playback.
    struct CameraTrack
    {
        std::vector<CameraKeyframe> keys;
        void                        validate() const;
        CameraPose                  evaluate(uint64_t frame) const;
        std::string                 serialize() const;
        static CameraTrack          parse(std::string_view text);
        void                        save(const std::filesystem::path& path) const;
        static CameraTrack          load(const std::filesystem::path& path);
    };
} // namespace vultra
