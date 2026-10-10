#pragma once

#include <vultra/core/image/image.hpp>
#include <vultra/scene/camera/camera_track.hpp>
#include <vultra/servers/rendering/research/stereo_renderer.hpp>

namespace vultra
{
    struct MethodConfiguration
    {
        std::string      name;
        MethodParameters parameters;
    };

    struct ResearchConfiguration
    {
        std::string           project;
        std::string           engineHash;
        std::filesystem::path modelOverride;
        std::filesystem::path environmentOverride;
        RenderSettings        renderer;
        CameraPose            camera;
        glm::mat4             rigView {1}; // Exact view avoids quaternion round-trip changes in quantized warping.
        glm::mat4 trackingPose {1}; // Captured head-to-tracking-origin pose; desktop freezes it, live XR ignores it.
        std::optional<HeadsetProfile>      headset;
        std::optional<CameraTrack>         track;
        uint64_t                           trackFrame = 0;
        std::array<Extent, 2>              sizes {{{640, 480}, {640, 480}}};
        std::array<MethodConfiguration, 2> methods;
        float                              ipd               = 0.064f;
        float                              xrScale           = 1;
        float                              differenceGain    = 4;
        int                                view              = 1;
        bool                               referenceSnapshot = false;
        MetricRegion                       roi;
        float                              pixelsPerDegree = 67;
        // Optional active graph texture names, R >= .5 selects a pixel. Projects define mask semantics.
        std::array<std::string, 2> masks;

        void                         validate() const;
        std::string                  serialize() const;
        static ResearchConfiguration parse(std::string_view text);
        void                         save(const std::filesystem::path& path) const;
        static ResearchConfiguration load(const std::filesystem::path& path);
    };
} // namespace vultra
