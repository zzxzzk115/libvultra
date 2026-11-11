#pragma once

#include <vultra/gpu/resources.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace vultra
{
    enum class RenderLightKind : uint32_t
    {
        eDirectional,
        ePoint,
        eSpot
    };

    constexpr uint32_t kMaxRenderLights = 64;

    struct RenderLight
    {
        RenderLightKind kind = RenderLightKind::eDirectional;
        glm::vec3       position {0};
        glm::vec3       directionToLight {0, 0, 1};
        glm::vec3       color {1};
        float           intensity = 1;
        float           range     = 10;
        float           innerCone = 0.35f; // Radians, measured from the spotlight axis.
        float           outerCone = 0.6f;
    };

    // One light as shaders read it. Keep in sync with LightData in builtin/shaders/resources/scene_lights.slangh.
    struct GpuLight
    {
        glm::vec4 positionRange;  // xyz world position, w range.
        glm::vec4 directionKind;  // xyz unit direction to the light, w RenderLightKind.
        glm::vec4 colorIntensity; // rgb colour, w intensity.
        glm::vec4 cones;          // Cosines of the inner and outer spot angles.
    };

    // The name a render path gives this buffer when it imports it into a graph.
    inline constexpr std::string_view kSceneLights = "scene.lights";

    // The analytic lights of a frame as one storage buffer of GpuLight records. Every render path reads lights
    // through it: bound directly, or imported into the graph for passes that build their own light structures.
    // Its capacity is fixed at kMaxRenderLights, so the buffer and its view stay valid for the object's lifetime.
    class LightBuffer
    {
    public:
        explicit LightBuffer(Device& device);
        ~LightBuffer();
        LightBuffer(const LightBuffer&)            = delete;
        LightBuffer& operator=(const LightBuffer&) = delete;

        // Validates the lights and uploads them. The frame that last read the buffer must have completed.
        void update(std::span<const RenderLight> lights);

        Buffer&                   buffer();
        VriDescriptor*            view() const; // Structured-buffer view of all kMaxRenderLights records.
        std::span<const GpuLight> lights() const;
        // Index of the first directional light, or -1 when there is none.
        int32_t mainDirectional() const;

    private:
        Device&                                m_Device;
        Buffer                                 m_Buffer;
        VriDescriptor*                         m_View = nullptr;
        std::array<GpuLight, kMaxRenderLights> m_Lights {};
        uint32_t                               m_Count           = 0;
        int32_t                                m_MainDirectional = -1;
    };
} // namespace vultra
