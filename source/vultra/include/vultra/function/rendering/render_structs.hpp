#pragma once

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/resource/gpu_scene_database.hpp"
#include "vultra/function/resource/gpu_scene_view.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class Texture;
    }
    class World;

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
        World*        worldOverride {nullptr};
        glm::vec4     clearValue {0, 0, 0, 1};
        uint32_t      clearMode {0};
        bool          renderImGui {true};
        bool          debugEntityIdOutput {false};
        bool          selectionOutlineEnabled {false};

        // SRP binding (string key, resolved to a Renderer instance by RenderSystem)
        // Example: "universal", "hd"
        std::string rendererKey {"universal"};
    };

    // Cooked render instance extracted from World.
    // Renderer consumes RenderWorld only.
    struct RenderInstance
    {
        struct MaterialOverride
        {
            uint32_t slot {0};
            uint32_t materialIndex {0};
        };

        CoreUUID  entity;
        uint32_t  meshIndex {0};
        uint32_t  materialIndex {0};
        glm::mat4 worldMatrix {1.0f};
        glm::vec4 baseColorOverride {1.0f};
        bool      hasBaseColorOverride {false};
        std::vector<MaterialOverride> materialOverrides;
    };

    struct RenderGaussianSplatInstance
    {
        CoreUUID  entity;
        uint32_t  splatIndex {0};
        glm::mat4 worldMatrix {1.0f};
    };

    [[nodiscard]] inline uint32_t makeEntityPickingId(const CoreUUID& entity)
    {
        if (!entity.valid())
            return 0u;
        uint32_t id = static_cast<uint32_t>(std::hash<CoreUUID> {}(entity) & 0x00FFFFFFu);
        return id == 0u ? 1u : id;
    }

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

    struct RenderEnvironment
    {
        bool      active {false};
        glm::vec3 ambientColor {0.15f};
        float     ambientIntensity {1.0f};
        bool      enableIBL {false};
        glm::vec3 iblColor {0.04f, 0.045f, 0.05f};
        float     iblIntensity {1.0f};
        rhi::Texture* skybox {nullptr};
    };

    enum class RenderReflectionProbeShape : uint32_t
    {
        eBox = 0,
        eSphere,
    };

    struct RenderReflectionProbe
    {
        CoreUUID entity;
        glm::vec3 position {0.0f};
        glm::vec3 halfExtents {5.0f};
        float     radius {5.0f};
        float     blendDistance {1.0f};
        float     intensity {1.0f};
        int       priority {0};
        bool      enableIBL {true};
        bool      parallaxCorrection {true};
        RenderReflectionProbeShape shape {RenderReflectionProbeShape::eBox};
        rhi::Texture* environmentMap {nullptr};
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

    struct SsaoRenderSettings
    {
        bool  enabled {true};
        float radius {1.5f};
        float bias {0.05f};
        float intensity {1.0f};
        int   maxRadiusPixels {32};
        int   stepCount {4};
        int   directionCount {8};
    };

    struct SsrRenderSettings
    {
        bool  enabled {true};
        float reflectionFactor {0.2f};
        int   maxSteps {16};
        int   binaryRefinement {3};
        float stride {0.35f};
        float thickness {0.5f};
    };

    struct ToneMappingRenderSettings
    {
        bool  enabled {false};
        float exposure {1.0f};
        int   method {0};
    };

    struct ShadowRenderSettings
    {
        enum class FilterMode : int
        {
            eHard = 0,
            ePCF  = 1,
            ePCSS = 2,
        };

        enum class DebugMode : int
        {
            eOff         = 0,
            eCascade     = 1,
            eVisibility  = 2,
            eShadowDepth = 3,
            eShadowCoord = 4,
            eAtlasUV     = 5,
        };

        bool      enabled {true};
        uint32_t resolution {4096};
        uint32_t cascadeCount {4};
        float     coverageRadius {75.0f};
        float     lightDistance {200.0f};
        float     zRange {120.0f};
        float     splitLambda {0.60f};
        bool      autoFitBounds {true};
        glm::vec3 lightDirection {-0.35f, -0.8f, -0.25f};
        float     depthBias {0.0012f};
        float     normalBias {0.015f};
        bool      stableTexelSnapping {true};
        FilterMode filterMode {FilterMode::ePCF};
        DebugMode  debugMode {DebugMode::eOff};
        float     pcssLightRadius {1.5f};
        int       pcssBlockerSamples {12};
        int       pcssFilterSamples {2};
    };

    struct PbrLightingSettings
    {
        enum class DebugViewMode : int
        {
            eLit = 0,
            eAlbedo,
            eNormal,
            eMetallic,
            eRoughness,
            eAO,
            eLinearDepth,
        };

        glm::vec3 directionalLightDirection {-0.35f, -0.8f, -0.25f};
        float     shadowStrength {0.85f};
        glm::vec3 directionalLightColor {1.0f, 0.96f, 0.9f};
        float     directionalLightIntensity {8.0f};
        glm::vec3 ambientColor {0.15f};
        float     ambientIntensity {1.0f};
        bool      enableIBL {false};
        glm::vec3 iblColor {0.04f, 0.045f, 0.05f};
        float     iblIntensity {1.0f};
        bool      showSkybox {true};
        rhi::Texture* environmentMap {nullptr};
        DebugViewMode debugViewMode {DebugViewMode::eLit};
    };

    struct BuiltinRenderSettings
    {
        struct SelectionOutlineSettings
        {
            bool      enabled {true};
            uint32_t  selectedEntityId {0u};
            glm::vec4 color {1.0f, 0.55f, 0.08f, 1.0f};
            float     thickness {3.0f};
            float     fillOpacity {0.0f};
            float     edgeOpacity {0.35f};
        };

        SsaoRenderSettings ssao;
        SsrRenderSettings  ssr;
        ToneMappingRenderSettings toneMapping;
        ShadowRenderSettings shadow;
        PbrLightingSettings pbrLighting;
        SelectionOutlineSettings selectionOutline;
        bool               xrMirrorGammaCorrect {false};
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
        RenderEnvironment           environment;
        std::vector<RenderReflectionProbe> reflectionProbes;
        bool                        hasBounds {false};
        glm::vec3                   boundsMin {0.0f};
        glm::vec3                   boundsMax {0.0f};

        resource::GpuSceneDatabase* gpuSceneDatabase {nullptr};
        resource::GpuSceneView*     gpuSceneView {nullptr};

        void clear()
        {
            cameras.clear();
            instances.clear();
            gaussianSplats.clear();
            lights.clear();
            environment = {};
            reflectionProbes.clear();
            hasBounds = false;
            boundsMin = glm::vec3 {0.0f};
            boundsMax = glm::vec3 {0.0f};
        }
    };

    [[nodiscard]] inline glm::vec3 renderCameraPosition(const RenderCamera* camera)
    {
        return camera ? glm::vec3(camera->inverseView[3]) : glm::vec3 {0.0f};
    }

    [[nodiscard]] inline float reflectionProbeScore(const RenderReflectionProbe& probe, const glm::vec3 position)
    {
        const auto delta = position - probe.position;
        float      distance = 0.0f;

        if (probe.shape == RenderReflectionProbeShape::eSphere)
        {
            distance = glm::length(delta);
            if (distance > probe.radius + probe.blendDistance)
                return -std::numeric_limits<float>::infinity();
        }
        else
        {
            const glm::vec3 localAbs {std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)};
            const glm::vec3 outer = probe.halfExtents + glm::vec3 {probe.blendDistance};
            if (localAbs.x > outer.x || localAbs.y > outer.y || localAbs.z > outer.z)
                return -std::numeric_limits<float>::infinity();

            const auto innerDelta = glm::max(localAbs - probe.halfExtents, glm::vec3 {0.0f});
            distance = glm::length(innerDelta);
        }

        return static_cast<float>(probe.priority) * 100000.0f - distance;
    }

    [[nodiscard]] inline const RenderReflectionProbe* selectReflectionProbe(const RenderWorld* world,
                                                                            const RenderCamera* camera)
    {
        if (!world || world->reflectionProbes.empty())
            return nullptr;

        const auto             position = renderCameraPosition(camera);
        const RenderReflectionProbe* best = nullptr;
        float                  bestScore = -std::numeric_limits<float>::infinity();
        for (const auto& probe : world->reflectionProbes)
        {
            if (probe.enableIBL && !probe.environmentMap)
                continue;
            const auto score = reflectionProbeScore(probe, position);
            if (score > bestScore)
            {
                best = &probe;
                bestScore = score;
            }
        }
        return best;
    }
} // namespace vultra
