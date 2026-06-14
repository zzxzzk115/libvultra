#include "vultra/function/scripting/bindings/script_render_shim.hpp"

#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/runtime_profiler.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/frame_debugger_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/world/components/camera_component.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace vultra
{
    namespace
    {
        ScriptVec2 toScriptVec2(const glm::vec2& v) { return {v.x, v.y}; }
        ScriptVec3 toScriptVec3(const glm::vec3& v) { return {v.x, v.y, v.z}; }
        glm::vec2  toGlmVec2(const ScriptVec2& v) { return {v.x, v.y}; }
        glm::vec3  toGlmVec3(const ScriptVec3& v) { return {v.x, v.y, v.z}; }

        ScriptGaussianSplatSettings toScriptSettings(const GaussianSplatRenderSettings& s)
        {
            return {.baselineMode                    = static_cast<int>(s.baselineMode),
                    .lodBudget                       = s.lodBudget,
                    .clodLevel                       = s.clodLevel,
                    .foveatedClodEnabled             = s.foveatedClodEnabled,
                    .foveatedRenderMode              = static_cast<int>(s.foveatedRenderMode),
                    .foveatedGaze                    = toScriptVec2(s.foveatedGaze),
                    .foveatedRingDegrees             = toScriptVec2(s.foveatedRingDegrees),
                    .foveatedRingLevels              = toScriptVec3(s.foveatedRingLevels),
                    .foveatedResolutionScales        = toScriptVec3(s.foveatedResolutionScales),
                    .foveatedTransitionDegrees       = s.foveatedTransitionDegrees,
                    .foveatedBudgetControllerEnabled = s.foveatedBudgetControllerEnabled,
                    .foveatedTargetFrameMs           = s.foveatedTargetFrameMs,
                    .foveatedBudgetAdjustRate        = s.foveatedBudgetAdjustRate};
        }

        void applyScriptSettings(GaussianSplatRenderSettings& dst, const ScriptGaussianSplatSettings& src)
        {
            dst.baselineMode                    = static_cast<GaussianSplatBaselineMode>(src.baselineMode);
            dst.lodBudget                       = src.lodBudget;
            dst.clodLevel                       = src.clodLevel;
            dst.foveatedClodEnabled             = src.foveatedClodEnabled;
            dst.foveatedRenderMode              = static_cast<GaussianSplatFoveatedRenderMode>(src.foveatedRenderMode);
            dst.foveatedGaze                    = toGlmVec2(src.foveatedGaze);
            dst.foveatedRingDegrees             = toGlmVec2(src.foveatedRingDegrees);
            dst.foveatedRingLevels              = toGlmVec3(src.foveatedRingLevels);
            dst.foveatedResolutionScales        = toGlmVec3(src.foveatedResolutionScales);
            dst.foveatedTransitionDegrees       = src.foveatedTransitionDegrees;
            dst.foveatedBudgetControllerEnabled = src.foveatedBudgetControllerEnabled;
            dst.foveatedTargetFrameMs           = src.foveatedTargetFrameMs;
            dst.foveatedBudgetAdjustRate        = src.foveatedBudgetAdjustRate;
        }

        ScriptGaussianSplatFrameStats toScriptStats(const GaussianSplatFrameStats& s)
        {
            return {.frameIndex                      = s.frameIndex,
                    .baselineMode                    = static_cast<int>(s.baselineMode),
                    .foveatedRenderMode              = static_cast<int>(s.foveatedRenderMode),
                    .lodBudgetEnabled                = s.lodBudgetEnabled,
                    .foveatedClodEnabled             = s.foveatedClodEnabled,
                    .foveatedLayeredCompositeEnabled = s.foveatedLayeredCompositeEnabled,
                    .foveatedBudgetControllerEnabled = s.foveatedBudgetControllerEnabled,
                    .directPrefix                    = s.directPrefix,
                    .lodBudget                       = s.lodBudget,
                    .foveatedRingLevels              = toScriptVec3(s.foveatedRingLevels),
                    .foveatedResolutionScales        = toScriptVec3(s.foveatedResolutionScales),
                    .foveatedRingDegrees             = toScriptVec2(s.foveatedRingDegrees),
                    .foveatedTargetFrameMs           = s.foveatedTargetFrameMs,
                    .splatAssets                     = s.splatAssets,
                    .drawRecords                     = s.drawRecords,
                    .totalSplats                     = s.totalSplats,
                    .preparedSplats                  = s.preparedSplats,
                    .maxVisibleSplatCap              = s.maxVisibleSplatCap,
                    .lodSelectedRawSplats            = s.lodSelectedRawSplats,
                    .visibleSplats                   = s.visibleSplats,
                    .drawnSplats                     = s.drawnSplats};
        }
    } // namespace

    std::uint32_t cameraCount(ScriptContext& ctx)
    {
        return ctx.cameraService ? static_cast<std::uint32_t>(ctx.cameraService->cameras().size()) : 0u;
    }

    ScriptCameraOverlayInfo cameraOverlayInfo(ScriptContext& ctx)
    {
        if (!ctx.cameraService)
            return ScriptCameraOverlayInfo {};

        const auto info = ctx.cameraService->cameraControlOverlayInfo();
        return info ? ScriptCameraOverlayInfo {.enabled = info->enabled, .mode = static_cast<int>(info->mode)} :
                      ScriptCameraOverlayInfo {};
    }

    void cameraSetInputSuppressed(ScriptContext& ctx, bool suppressed)
    {
        if (ctx.cameraService)
            ctx.cameraService->setCameraControlInputSuppressed(suppressed);
    }

    ScriptEntity cameraFindPrimary(ScriptContext& ctx)
    {
        auto* world = ctx.world();
        if (!world)
            return ScriptEntity {};

        auto view = world->registry().view<CameraComponent>();
        for (auto entity : view)
        {
            if (view.get<CameraComponent>(entity).primary)
                return ScriptEntity {entity};
        }
        return ScriptEntity {};
    }

    void renderResize(ScriptContext& ctx, std::uint32_t width, std::uint32_t height)
    {
        if (ctx.renderService)
            ctx.renderService->onResize(width, height);
    }

    ScriptGaussianSplatSettings renderGaussianSplatSettings(ScriptContext& ctx)
    {
        return ctx.renderService ? toScriptSettings(ctx.renderService->gaussianSplatSettings()) :
                                   ScriptGaussianSplatSettings {};
    }

    void renderSetGaussianSplatSettings(ScriptContext& ctx, const ScriptGaussianSplatSettings& settings)
    {
        if (ctx.renderService)
            applyScriptSettings(ctx.renderService->gaussianSplatSettings(), settings);
    }

    ScriptGaussianSplatFrameStats renderGaussianSplatFrameStats(ScriptContext& ctx)
    {
        return ctx.renderService ? toScriptStats(ctx.renderService->gaussianSplatFrameStats()) :
                                   ScriptGaussianSplatFrameStats {};
    }

    void renderSetProfilerEnabled(ScriptContext& ctx, bool enabled)
    {
        if (ctx.renderService && ctx.renderService->runtimeProfiler())
            ctx.renderService->runtimeProfiler()->setEnabled(enabled);
    }

    bool renderIsProfilerEnabled(ScriptContext& ctx)
    {
        return ctx.renderService && ctx.renderService->runtimeProfiler() ?
                   ctx.renderService->runtimeProfiler()->isEnabled() :
                   false;
    }

    std::uint32_t renderProfilerHistorySize(ScriptContext& ctx)
    {
        return ctx.renderService && ctx.renderService->runtimeProfiler() ?
                   static_cast<std::uint32_t>(ctx.renderService->runtimeProfiler()->historySize()) :
                   0u;
    }

    void renderCaptureFrame(ScriptContext& ctx)
    {
        if (ctx.frameDebuggerService)
            ctx.frameDebuggerService->captureSingleFrame();
    }

    bool renderBackendIsXREnabled(ScriptContext& ctx)
    {
        return ctx.renderBackendService ? ctx.renderBackendService->isXREnabled() : false;
    }

    bool renderBackendIsXRMirrorEnabled(ScriptContext& ctx)
    {
        return ctx.renderBackendService ? ctx.renderBackendService->isXRMirrorEnabled() : false;
    }

    bool renderBackendIsExitRequested(ScriptContext& ctx)
    {
        return ctx.renderBackendService ? ctx.renderBackendService->isExitRequested() : false;
    }
} // namespace vultra
