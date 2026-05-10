#pragma once

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/resource/gpu_scene_database.hpp"
#include "vultra/function/resource/gpu_scene_view.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
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

    // Gaussian splat rendering is intentionally split into two orthogonal switches:
    // visibility selection (baseline vs. ordered CLOD prefix) and sort quality
    // (plain clip-depth vs. conservative depth). There is no hierarchy/proxy LOD
    // mode here; trained or imported importance is consumed only as an ordering.
    enum class GaussianSplatBaselineMode : uint8_t
    {
        eBaseline = 0,
        eConservativeSort,
        eOrderedClod,
        eOrderedClodAndConservativeSort,
    };

    enum class GaussianSplatSortMode : uint8_t
    {
        eClipDepth = 0,
        eDistance,
        eViewDepth,
        eConservativeDepth,
    };

    struct GaussianSplatRenderSettings
    {
        GaussianSplatBaselineMode baselineMode {GaussianSplatBaselineMode::eBaseline};
        uint32_t                  lodBudget {0}; // 0 means derive the selected count from clodLevel.
        float                     clodLevel {1.0f}; // Fraction of the ordered list to keep when lodBudget is automatic.

        // Optional distance modulation keeps nearby points at a higher ordered prefix
        // and fades far points out instead of swapping to proxy splats.
        bool                      clodDistanceLodEnabled {false};
        float                     clodMinDistance {1.0f};
        float                     clodMaxDistance {10.0f};
        float                     clodNearLod {1.0f};
        float                     clodFarLod {0.25f};
        float                     clodFadeWidth {0.2f};

        [[nodiscard]] bool conservativeSortEnabled() const
        {
            return baselineMode == GaussianSplatBaselineMode::eConservativeSort ||
                   baselineMode == GaussianSplatBaselineMode::eOrderedClodAndConservativeSort;
        }

        [[nodiscard]] bool orderedClodEnabled() const
        {
            return baselineMode == GaussianSplatBaselineMode::eOrderedClod ||
                   baselineMode == GaussianSplatBaselineMode::eOrderedClodAndConservativeSort;
        }

        [[nodiscard]] bool lodBudgetEnabled() const
        {
            return orderedClodEnabled();
        }

        [[nodiscard]] GaussianSplatSortMode sortMode() const
        {
            return conservativeSortEnabled() ? GaussianSplatSortMode::eConservativeDepth :
                                               GaussianSplatSortMode::eClipDepth;
        }
    };

    struct GaussianSplatFrameStats
    {
        uint64_t frameIndex {0};
        GaussianSplatBaselineMode baselineMode {GaussianSplatBaselineMode::eBaseline};
        GaussianSplatSortMode     sortMode {GaussianSplatSortMode::eClipDepth};
        bool                      lodBudgetEnabled {false};
        uint32_t                  lodBudget {0};

        uint32_t splatAssets {0};
        uint32_t drawRecords {0};
        uint32_t totalSplats {0};
        uint32_t preparedSplats {0};
        uint32_t maxVisibleSplatCap {0};
        uint32_t lodSelectedRawSplats {0};
        uint32_t lodTransitionSplats {0};

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
