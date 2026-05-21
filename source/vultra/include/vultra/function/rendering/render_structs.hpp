#pragma once

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/resource/gpu_scene_database.hpp"
#include "vultra/function/resource/gpu_scene_view.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class Texture;
    }

    // A cooked camera used by the renderer (SRP-style).
    // ECS CameraComponent should be cooked into this struct by CameraSystem.
    struct RenderCamera
    {
        CoreUUID uuid;

        // Debug / editor name (optional)
        std::string name;

        // Sorting
        int priority {0};

        // Multi-view metadata (mono: viewCount=1, stereo: viewCount=2)
        uint32_t viewIndex {0};
        uint32_t viewCount {1};
        bool     isXRView {false};
        bool     isXRPrimaryView {true};

        // Matrices
        glm::mat4 view {1.0f};
        glm::mat4 projection {1.0f};
        glm::mat4 viewProjection {1.0f};
        glm::mat4 inverseView {1.0f};
        glm::mat4 inverseProjection {1.0f};
        glm::mat4 inverseViewProjection {1.0f};

        float zNear {0.1f};
        float zFar {1000.0f};
        float fovY {glm::radians(60.0f)};

        std::array<glm::vec4, 6> frustumPlanes {glm::vec4(0.0f),
                                                glm::vec4(0.0f),
                                                glm::vec4(0.0f),
                                                glm::vec4(0.0f),
                                                glm::vec4(0.0f),
                                                glm::vec4(0.0f)};

        // Render target (nullptr => backbuffer or XR-provided target)
        rhi::Texture* target {nullptr};
        glm::vec4     clearValue {0, 0, 0, 1};
        bool          renderImGui {true};

        // SRP binding (string key, resolved to a Renderer instance by RenderSystem)
        // Example: "universal", "hd"
        std::string rendererKey {"universal"};
    };

    // Cooked render instance extracted from World.
    // Renderer consumes RenderWorld only.
    struct RenderInstance
    {
        CoreUUID  entity;
        uint32_t  meshIndex {0};
        uint32_t  materialIndex {0};
        glm::mat4 worldMatrix {1.0f};
        glm::vec4 baseColorOverride {1.0f};
        bool      hasBaseColorOverride {false};
    };

    struct RenderGaussianSplatInstance
    {
        CoreUUID  entity;
        uint32_t  splatIndex {0};
        glm::mat4 worldMatrix {1.0f};
    };

    enum class RenderLightKind : uint32_t
    {
        eDirectional = 0,
        ePoint,
        eSpot,
        eArea,
    };

    struct RenderLight
    {
        CoreUUID        entity;
        RenderLightKind kind {RenderLightKind::eDirectional};
        glm::vec3       position {0.0f};
        float           range {10.0f};
        glm::vec3       direction {-0.35f, -0.8f, -0.25f};
        float           intensity {3.0f};
        glm::vec3       color {1.0f};
        float           radius {0.05f};
        float           width {1.0f};
        float           height {1.0f};
        float           innerConeDegrees {20.0f};
        float           outerConeDegrees {30.0f};
        bool            castsShadow {true};
        bool            twoSided {false};
    };

    // Gaussian splat rendering has one LOD path: imported/trained assets are
    // physically sorted by importance, and Ordered CLOD renders a prefix of that
    // packed order. There is no hierarchy/proxy LOD or runtime importance sort.
    enum class GaussianSplatBaselineMode : uint8_t
    {
        eBaseline = 0,
        eOrderedClod,
    };

    enum class GaussianSplatFoveatedRenderMode : uint8_t
    {
        eSinglePass = 0,
        eLayeredComposite,
    };

    inline constexpr uint32_t kGaussianSplatFoveatedLayerCount =
        resource::kGeneralGaussianSplatFoveatedLayerCount;

    struct GaussianSplatFoveatedLayerConfig
    {
        float eccentricityDegrees {0.0f};
        float resolutionScale {1.0f};
        float lodLevel {1.0f};
    };

    struct GaussianSplatRenderSettings
    {
        GaussianSplatBaselineMode       baselineMode {GaussianSplatBaselineMode::eBaseline};
        uint32_t                        lodBudget {0}; // 0 means derive the selected count from clodLevel.
        float                           clodLevel {1.0f}; // Fraction of the ordered list to keep when lodBudget is automatic.
        bool                            foveatedClodEnabled {false};
        GaussianSplatFoveatedRenderMode foveatedRenderMode {GaussianSplatFoveatedRenderMode::eSinglePass};
        glm::vec2                       foveatedGaze {0.5f, 0.5f}; // Viewport UV, top-left origin.
        glm::vec2                       foveatedRingDegrees {5.0f, 15.0f};
        glm::vec3                       foveatedRingLevels {1.0f, 0.25f, 0.05f};
        glm::vec3                       foveatedResolutionScales {1.0f, 0.5f, 0.25f};
        float                           foveatedTransitionDegrees {2.0f};
        bool                            foveatedBudgetControllerEnabled {false};
        float                           foveatedTargetFrameMs {11.1f};
        float                           foveatedBudgetAdjustRate {0.05f};

        [[nodiscard]] std::array<GaussianSplatFoveatedLayerConfig, kGaussianSplatFoveatedLayerCount> foveatedLayers() const
        {
            return {
                GaussianSplatFoveatedLayerConfig {
                    .eccentricityDegrees = foveatedRingDegrees.x,
                    .resolutionScale     = foveatedResolutionScales.x,
                    .lodLevel            = foveatedRingLevels.x,
                },
                GaussianSplatFoveatedLayerConfig {
                    .eccentricityDegrees = foveatedRingDegrees.y,
                    .resolutionScale     = foveatedResolutionScales.y,
                    .lodLevel            = foveatedRingLevels.y,
                },
                GaussianSplatFoveatedLayerConfig {
                    .eccentricityDegrees = 180.0f,
                    .resolutionScale     = foveatedResolutionScales.z,
                    .lodLevel            = foveatedRingLevels.z,
                },
            };
        }

        [[nodiscard]] bool orderedClodEnabled() const
        {
            return baselineMode == GaussianSplatBaselineMode::eOrderedClod;
        }

        [[nodiscard]] bool lodBudgetEnabled() const
        {
            return orderedClodEnabled();
        }

        [[nodiscard]] bool foveatedClodActive() const
        {
            return orderedClodEnabled() && foveatedClodEnabled;
        }

        [[nodiscard]] bool foveatedLayeredCompositeActive() const
        {
            return foveatedClodActive() && foveatedRenderMode == GaussianSplatFoveatedRenderMode::eLayeredComposite;
        }
    };

    struct GaussianSplatFrameStats
    {
        uint64_t                        frameIndex {0};
        GaussianSplatBaselineMode       baselineMode {GaussianSplatBaselineMode::eBaseline};
        GaussianSplatFoveatedRenderMode foveatedRenderMode {GaussianSplatFoveatedRenderMode::eSinglePass};
        bool                            lodBudgetEnabled {false};
        bool                            foveatedClodEnabled {false};
        bool                            foveatedLayeredCompositeEnabled {false};
        bool                            foveatedBudgetControllerEnabled {false};
        bool                            directPrefix {false};
        uint32_t                        lodBudget {0};
        glm::vec3                       foveatedRingLevels {1.0f, 0.25f, 0.05f};
        glm::vec3                       foveatedResolutionScales {1.0f, 0.5f, 0.25f};
        glm::vec2                       foveatedRingDegrees {5.0f, 15.0f};
        float                           foveatedTargetFrameMs {11.1f};

        uint32_t splatAssets {0};
        uint32_t drawRecords {0};
        uint32_t totalSplats {0};
        uint32_t preparedSplats {0};
        uint32_t maxVisibleSplatCap {0};
        uint32_t lodSelectedRawSplats {0};

        // GPU readback for these counters is intentionally left out of stage 0.
        uint32_t visibleSplats {UINT32_MAX};
        uint32_t drawnSplats {UINT32_MAX};
    };

    struct HbaoRenderSettings
    {
        bool  enabled {false};
        float radius {80.0f};
        float bias {0.2f};
        float intensity {4.0f};
        int   maxRadiusPixels {256};
        int   stepCount {4};
        int   directionCount {4};
    };

    struct SsrRenderSettings
    {
        bool  enabled {false};
        float reflectionFactor {1.0f};
        int   maxSteps {32};
        int   binaryRefinement {6};
        float stride {0.1f};
        float thickness {1.0f};
    };

    struct ShadowRenderSettings
    {
        bool      enabled {true};
        uint32_t resolution {2048};
        uint32_t cascadeCount {4};
        float     coverageRadius {80.0f};
        float     lightDistance {80.0f};
        float     zRange {160.0f};
        glm::vec3 lightDirection {-0.35f, -0.8f, -0.25f};
        float     depthBias {0.0015f};
        float     normalBias {0.02f};
        float     pcssLightRadius {2.5f};
        int       pcssBlockerSamples {8};
        int       pcssFilterSamples {1};
    };

    struct PbrLightingSettings
    {
        glm::vec3 directionalLightDirection {-0.35f, -0.8f, -0.25f};
        float     shadowStrength {0.85f};
        glm::vec3 directionalLightColor {1.0f, 0.96f, 0.9f};
        float     directionalLightIntensity {8.0f};
        glm::vec3 ambientColor {0.15f};
        float     ambientIntensity {1.0f};
        bool      enableIBL {false};
        glm::vec3 iblColor {0.04f, 0.045f, 0.05f};
        float     iblIntensity {0.0f};
    };

    struct BuiltinRenderSettings
    {
        HbaoRenderSettings hbao;
        SsrRenderSettings  ssr;
        ShadowRenderSettings shadow;
        PbrLightingSettings pbrLighting;
        bool               enableFXAA {true};
    };

    // Double-buffered cooked scene for rendering.
    struct RenderWorld
    {
        uint64_t                    frameIndex {0};
        std::vector<RenderCamera>   cameras;
        std::vector<RenderInstance> instances;
        std::vector<RenderGaussianSplatInstance> gaussianSplats;
        std::vector<RenderLight>    lights;

        resource::GpuSceneDatabase* gpuSceneDatabase {nullptr};
        resource::GpuSceneView*     gpuSceneView {nullptr};

        void clear()
        {
            cameras.clear();
            instances.clear();
            gaussianSplats.clear();
            lights.clear();
        }
    };
} // namespace vultra
