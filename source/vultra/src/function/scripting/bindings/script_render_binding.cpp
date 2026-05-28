#include "vultra/function/scripting/bindings/script_render_binding.hpp"

#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/runtime_profiler.hpp"
#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/frame_debugger_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/render_service.hpp"

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

    void registerScriptRenderBindings(sol::state& lua, ScriptContext& ctx)
    {
        lua.new_usertype<ScriptCameraOverlayInfo>(
            "CameraOverlayInfo", "enabled", &ScriptCameraOverlayInfo::enabled, "mode", &ScriptCameraOverlayInfo::mode);

        lua.new_usertype<ScriptGaussianSplatSettings>("GaussianSplatSettings",
                                                      "baselineMode",
                                                      &ScriptGaussianSplatSettings::baselineMode,
                                                      "lodBudget",
                                                      &ScriptGaussianSplatSettings::lodBudget,
                                                      "clodLevel",
                                                      &ScriptGaussianSplatSettings::clodLevel,
                                                      "foveatedClodEnabled",
                                                      &ScriptGaussianSplatSettings::foveatedClodEnabled,
                                                      "foveatedRenderMode",
                                                      &ScriptGaussianSplatSettings::foveatedRenderMode,
                                                      "foveatedGaze",
                                                      &ScriptGaussianSplatSettings::foveatedGaze,
                                                      "foveatedRingDegrees",
                                                      &ScriptGaussianSplatSettings::foveatedRingDegrees,
                                                      "foveatedRingLevels",
                                                      &ScriptGaussianSplatSettings::foveatedRingLevels,
                                                      "foveatedResolutionScales",
                                                      &ScriptGaussianSplatSettings::foveatedResolutionScales,
                                                      "foveatedTransitionDegrees",
                                                      &ScriptGaussianSplatSettings::foveatedTransitionDegrees,
                                                      "foveatedBudgetControllerEnabled",
                                                      &ScriptGaussianSplatSettings::foveatedBudgetControllerEnabled,
                                                      "foveatedTargetFrameMs",
                                                      &ScriptGaussianSplatSettings::foveatedTargetFrameMs,
                                                      "foveatedBudgetAdjustRate",
                                                      &ScriptGaussianSplatSettings::foveatedBudgetAdjustRate);

        lua.new_usertype<ScriptGaussianSplatFrameStats>("GaussianSplatFrameStats",
                                                        "frameIndex",
                                                        &ScriptGaussianSplatFrameStats::frameIndex,
                                                        "baselineMode",
                                                        &ScriptGaussianSplatFrameStats::baselineMode,
                                                        "foveatedRenderMode",
                                                        &ScriptGaussianSplatFrameStats::foveatedRenderMode,
                                                        "lodBudgetEnabled",
                                                        &ScriptGaussianSplatFrameStats::lodBudgetEnabled,
                                                        "foveatedClodEnabled",
                                                        &ScriptGaussianSplatFrameStats::foveatedClodEnabled,
                                                        "foveatedLayeredCompositeEnabled",
                                                        &ScriptGaussianSplatFrameStats::foveatedLayeredCompositeEnabled,
                                                        "foveatedBudgetControllerEnabled",
                                                        &ScriptGaussianSplatFrameStats::foveatedBudgetControllerEnabled,
                                                        "directPrefix",
                                                        &ScriptGaussianSplatFrameStats::directPrefix,
                                                        "lodBudget",
                                                        &ScriptGaussianSplatFrameStats::lodBudget,
                                                        "foveatedRingLevels",
                                                        &ScriptGaussianSplatFrameStats::foveatedRingLevels,
                                                        "foveatedResolutionScales",
                                                        &ScriptGaussianSplatFrameStats::foveatedResolutionScales,
                                                        "foveatedRingDegrees",
                                                        &ScriptGaussianSplatFrameStats::foveatedRingDegrees,
                                                        "foveatedTargetFrameMs",
                                                        &ScriptGaussianSplatFrameStats::foveatedTargetFrameMs,
                                                        "splatAssets",
                                                        &ScriptGaussianSplatFrameStats::splatAssets,
                                                        "drawRecords",
                                                        &ScriptGaussianSplatFrameStats::drawRecords,
                                                        "totalSplats",
                                                        &ScriptGaussianSplatFrameStats::totalSplats,
                                                        "preparedSplats",
                                                        &ScriptGaussianSplatFrameStats::preparedSplats,
                                                        "maxVisibleSplatCap",
                                                        &ScriptGaussianSplatFrameStats::maxVisibleSplatCap,
                                                        "lodSelectedRawSplats",
                                                        &ScriptGaussianSplatFrameStats::lodSelectedRawSplats,
                                                        "visibleSplats",
                                                        &ScriptGaussianSplatFrameStats::visibleSplats,
                                                        "drawnSplats",
                                                        &ScriptGaussianSplatFrameStats::drawnSplats);

        auto camera = script_binding::getOrCreateTable(lua, "Camera");
        camera.set_function("count", [&ctx]() {
            return ctx.cameraService ? static_cast<uint32_t>(ctx.cameraService->cameras().size()) : 0u;
        });
        camera.set_function("overlayInfo", [&ctx]() {
            if (!ctx.cameraService)
                return ScriptCameraOverlayInfo {};

            const auto info = ctx.cameraService->cameraControlOverlayInfo();
            return info ? ScriptCameraOverlayInfo {.enabled = info->enabled, .mode = static_cast<int>(info->mode)} :
                          ScriptCameraOverlayInfo {};
        });
        camera.set_function("setInputSuppressed", [&ctx](bool suppressed) {
            if (ctx.cameraService)
                ctx.cameraService->setCameraControlInputSuppressed(suppressed);
        });

        auto render = script_binding::getOrCreateTable(lua, "Render");
        render.set_function("resize", [&ctx](uint32_t width, uint32_t height) {
            if (ctx.renderService)
                ctx.renderService->onResize(width, height);
        });
        render.set_function("getGaussianSplatSettings", [&ctx]() {
            return ctx.renderService ? toScriptSettings(ctx.renderService->gaussianSplatSettings()) :
                                       ScriptGaussianSplatSettings {};
        });
        render.set_function("setGaussianSplatSettings", [&ctx](const ScriptGaussianSplatSettings& settings) {
            if (ctx.renderService)
                applyScriptSettings(ctx.renderService->gaussianSplatSettings(), settings);
        });
        render.set_function("getGaussianSplatFrameStats", [&ctx]() {
            return ctx.renderService ? toScriptStats(ctx.renderService->gaussianSplatFrameStats()) :
                                       ScriptGaussianSplatFrameStats {};
        });
        render.set_function("setProfilerEnabled", [&ctx](bool enabled) {
            if (ctx.renderService && ctx.renderService->runtimeProfiler())
                ctx.renderService->runtimeProfiler()->setEnabled(enabled);
        });
        render.set_function("isProfilerEnabled", [&ctx]() {
            return ctx.renderService && ctx.renderService->runtimeProfiler() ?
                       ctx.renderService->runtimeProfiler()->isEnabled() :
                       false;
        });
        render.set_function("profilerHistorySize", [&ctx]() {
            return ctx.renderService && ctx.renderService->runtimeProfiler() ?
                       static_cast<uint32_t>(ctx.renderService->runtimeProfiler()->historySize()) :
                       0u;
        });
        render.set_function("captureFrame", [&ctx]() {
            if (ctx.frameDebuggerService)
                ctx.frameDebuggerService->captureSingleFrame();
        });

        auto backend = script_binding::getOrCreateTable(lua, "RenderBackend");
        backend.set_function("isXREnabled", [&ctx]() {
            return ctx.renderBackendService ? ctx.renderBackendService->isXREnabled() : false;
        });
        backend.set_function("isXRMirrorEnabled", [&ctx]() {
            return ctx.renderBackendService ? ctx.renderBackendService->isXRMirrorEnabled() : false;
        });
        backend.set_function("isExitRequested", [&ctx]() {
            return ctx.renderBackendService ? ctx.renderBackendService->isExitRequested() : false;
        });

        script_binding::bindEnumTable<CameraControlMode>(lua, "CameraControlMode");
        script_binding::bindEnumTable<GaussianSplatBaselineMode>(lua, "GaussianSplatBaselineMode");
        script_binding::bindEnumTable<GaussianSplatFoveatedRenderMode>(lua, "GaussianSplatFoveatedRenderMode");
    }
} // namespace vultra
