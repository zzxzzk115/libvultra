#pragma once

#include <entt/entity/entity.hpp>

#include <cstdint>
#include <string>

namespace vultra
{
    struct ScriptEntity
    {
        entt::entity value {entt::null};
    };

    struct ScriptTransformRef
    {
        entt::entity entity {entt::null};
    };

    struct ScriptVec2
    {
        float x {0.0f};
        float y {0.0f};
    };

    struct ScriptVec3
    {
        float x {0.0f};
        float y {0.0f};
        float z {0.0f};
    };

    struct ScriptAssetHandle
    {
        bool        valid {false};
        bool        ready {false};
        std::string uuid;
        int         state {0};
        uint32_t    gpuIndex {UINT32_MAX};
    };

    struct ScriptTextAssetResult
    {
        bool        ok {false};
        std::string text;
        std::string error;
    };

    struct ScriptAssetMemoryStats
    {
        uint64_t cpuCacheBytes {0};
    };

    struct ScriptCameraOverlayInfo
    {
        bool enabled {false};
        int  mode {0};
    };

    struct ScriptGaussianSplatSettings
    {
        int      baselineMode {0};
        uint32_t lodBudget {0};
        float    clodLevel {1.0f};
        bool     foveatedClodEnabled {false};
        int      foveatedRenderMode {0};
        ScriptVec2 foveatedGaze {0.5f, 0.5f};
        ScriptVec2 foveatedRingDegrees {5.0f, 15.0f};
        ScriptVec3 foveatedRingLevels {1.0f, 0.25f, 0.05f};
        ScriptVec3 foveatedResolutionScales {1.0f, 0.5f, 0.25f};
        float      foveatedTransitionDegrees {2.0f};
        bool       foveatedBudgetControllerEnabled {false};
        float      foveatedTargetFrameMs {11.1f};
        float      foveatedBudgetAdjustRate {0.05f};
    };

    struct ScriptGaussianSplatFrameStats
    {
        uint64_t frameIndex {0};
        int      baselineMode {0};
        int      foveatedRenderMode {0};
        bool     lodBudgetEnabled {false};
        bool     foveatedClodEnabled {false};
        bool     foveatedLayeredCompositeEnabled {false};
        bool     foveatedBudgetControllerEnabled {false};
        bool     directPrefix {false};
        uint32_t lodBudget {0};
        ScriptVec3 foveatedRingLevels {1.0f, 0.25f, 0.05f};
        ScriptVec3 foveatedResolutionScales {1.0f, 0.5f, 0.25f};
        ScriptVec2 foveatedRingDegrees {5.0f, 15.0f};
        float      foveatedTargetFrameMs {11.1f};
        uint32_t splatAssets {0};
        uint32_t drawRecords {0};
        uint32_t totalSplats {0};
        uint32_t preparedSplats {0};
        uint32_t maxVisibleSplatCap {0};
        uint32_t lodSelectedRawSplats {0};
        uint32_t visibleSplats {UINT32_MAX};
        uint32_t drawnSplats {UINT32_MAX};
    };
} // namespace vultra
