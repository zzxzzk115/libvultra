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
    };

    struct RenderGaussianSplatInstance
    {
        CoreUUID  entity;
        uint32_t  splatIndex {0};
        glm::mat4 worldMatrix {1.0f};
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

    enum class GaussianSplatFoveatedAdaptationMode : uint8_t
    {
        eFixed = 0,
        eDynamicBudget,
        eDynamicRange,
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
        GaussianSplatBaselineMode           baselineMode {GaussianSplatBaselineMode::eBaseline};
        uint32_t                            lodBudget {0}; // 0 means derive the selected count from clodLevel.
        float                               clodLevel {1.0f}; // Fraction of the ordered list to keep when lodBudget is automatic.
        bool                                foveatedClodEnabled {false};
        bool                                foveatedManualGazeControlEnabled {false};
        bool                                foveatedCoverageCompensationEnabled {true};
        bool                                foveatedDebugOverlayEnabled {false};
        GaussianSplatFoveatedRenderMode     foveatedRenderMode {GaussianSplatFoveatedRenderMode::eSinglePass};
        glm::vec2                           foveatedGaze {0.5f, 0.5f}; // Viewport UV, top-left origin.
        glm::vec2                           foveatedRingDegrees {8.0f, 24.0f};
        glm::vec3                           foveatedRingLevels {1.0f, 0.40f, 0.15f};
        glm::vec3                           foveatedResolutionScales {1.0f, 0.75f, 0.50f};
        float                               foveatedTransitionDegrees {4.0f};
        GaussianSplatFoveatedAdaptationMode foveatedAdaptationMode {GaussianSplatFoveatedAdaptationMode::eFixed};
        float                               foveatedTargetFrameMs {11.1f};
        float                               foveatedBudgetAdjustRate {0.05f};

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

    inline void applyGaussianSplatFovGsStyleBaseline(GaussianSplatRenderSettings& settings)
    {
        settings.baselineMode                     = GaussianSplatBaselineMode::eOrderedClod;
        settings.lodBudget                        = 0u;
        settings.clodLevel                        = 1.0f;
        settings.foveatedClodEnabled              = true;
        settings.foveatedManualGazeControlEnabled = false;
        settings.foveatedCoverageCompensationEnabled = false;
        settings.foveatedDebugOverlayEnabled      = false;
        settings.foveatedRenderMode               = GaussianSplatFoveatedRenderMode::eSinglePass;
        settings.foveatedRingDegrees              = glm::vec2 {10.0f, 42.0f};
        settings.foveatedRingLevels               = glm::vec3 {1.0f, 0.25f, 0.125f};
        settings.foveatedResolutionScales         = glm::vec3 {1.0f, 1.0f, 1.0f};
        settings.foveatedTransitionDegrees        = 4.0f;
        settings.foveatedAdaptationMode           = GaussianSplatFoveatedAdaptationMode::eFixed;
    }

    struct GaussianSplatFrameStats
    {
        uint64_t                            frameIndex {0};
        GaussianSplatBaselineMode           baselineMode {GaussianSplatBaselineMode::eBaseline};
        GaussianSplatFoveatedRenderMode     foveatedRenderMode {GaussianSplatFoveatedRenderMode::eSinglePass};
        bool                                lodBudgetEnabled {false};
        bool                                foveatedClodEnabled {false};
        bool                                foveatedLayeredCompositeEnabled {false};
        bool                                foveatedCoverageCompensationEnabled {false};
        bool                                foveatedDebugOverlayEnabled {false};
        GaussianSplatFoveatedAdaptationMode foveatedAdaptationMode {GaussianSplatFoveatedAdaptationMode::eFixed};
        bool                                directPrefix {false};
        uint32_t                            lodBudget {0};
        glm::vec3                           foveatedRingLevels {1.0f, 0.40f, 0.15f};
        glm::vec3                           foveatedResolutionScales {1.0f, 0.75f, 0.50f};
        glm::vec2                           foveatedGaze {0.5f, 0.5f};
        glm::vec2                           foveatedRingDegrees {8.0f, 24.0f};
        float                               foveatedTargetFrameMs {11.1f};

        uint32_t splatAssets {0};
        uint32_t drawRecords {0};
        uint32_t totalSplats {0};
        uint32_t preparedSplats {0};
        uint32_t foveaSplatBudget {0};
        uint32_t midSplatBudget {0};
        uint32_t outerSplatBudget {0};
        uint32_t maxVisibleSplatCap {0};
        uint32_t lodSelectedRawSplats {0};

        // GPU readback for these counters is intentionally left out of stage 0.
        uint32_t visibleSplats {UINT32_MAX};
        uint32_t drawnSplats {UINT32_MAX};
    };

    // Double-buffered cooked scene for rendering.
    struct RenderWorld
    {
        uint64_t                    frameIndex {0};
        std::vector<RenderCamera>   cameras;
        std::vector<RenderInstance> instances;
        std::vector<RenderGaussianSplatInstance> gaussianSplats;

        resource::GpuSceneDatabase* gpuSceneDatabase {nullptr};
        resource::GpuSceneView*     gpuSceneView {nullptr};

        void clear()
        {
            cameras.clear();
            instances.clear();
            gaussianSplats.clear();
        }
    };
} // namespace vultra
