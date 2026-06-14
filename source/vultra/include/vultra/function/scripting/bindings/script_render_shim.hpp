#pragma once

// Shim declarations for the Lua `Camera`, `Render`, and `RenderBackend`
// namespaces (all generated into the one `render` area / registrar). The bodies
// in script_render_shim.cpp own the engine<->script struct conversion and
// service access.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"

#include <cstdint>

namespace vultra
{
    struct VBIND_MODULE(name = Camera, area = render, service = cameraService) CameraModule
    {
    };
    struct VBIND_MODULE(name = Render, area = render, service = renderService) RenderModule
    {
    };
    struct VBIND_MODULE(name = RenderBackend, area = render, service = renderBackendService) RenderBackendModule
    {
    };

    VBIND_FN(module = Camera, name = count, body = shim)
    std::uint32_t cameraCount(ScriptContext& ctx);
    VBIND_FN(module = Camera, name = overlayInfo, body = shim)
    ScriptCameraOverlayInfo cameraOverlayInfo(ScriptContext& ctx);
    VBIND_FN(module = Camera, name = setInputSuppressed, body = shim)
    void cameraSetInputSuppressed(ScriptContext& ctx, bool suppressed);
    VBIND_FN(module = Camera, name = findPrimary, body = shim)
    ScriptEntity cameraFindPrimary(ScriptContext& ctx);

    VBIND_FN(module = Render, name = resize, body = shim)
    void renderResize(ScriptContext& ctx, std::uint32_t width, std::uint32_t height);
    VBIND_FN(module = Render, name = gaussianSplatSettings, body = shim)
    ScriptGaussianSplatSettings renderGaussianSplatSettings(ScriptContext& ctx);
    VBIND_FN(module = Render, name = setGaussianSplatSettings, body = shim)
    void renderSetGaussianSplatSettings(ScriptContext& ctx, const ScriptGaussianSplatSettings& settings);
    VBIND_FN(module = Render, name = gaussianSplatFrameStats, body = shim)
    ScriptGaussianSplatFrameStats renderGaussianSplatFrameStats(ScriptContext& ctx);
    VBIND_FN(module = Render, name = setProfilerEnabled, body = shim)
    void renderSetProfilerEnabled(ScriptContext& ctx, bool enabled);
    VBIND_FN(module = Render, name = isProfilerEnabled, body = shim)
    bool renderIsProfilerEnabled(ScriptContext& ctx);
    VBIND_FN(module = Render, name = profilerHistorySize, body = shim)
    std::uint32_t renderProfilerHistorySize(ScriptContext& ctx);
    VBIND_FN(module = Render, name = captureFrame, body = shim)
    void renderCaptureFrame(ScriptContext& ctx);

    VBIND_FN(module = RenderBackend, name = isXREnabled, body = shim)
    bool renderBackendIsXREnabled(ScriptContext& ctx);
    VBIND_FN(module = RenderBackend, name = isXRMirrorEnabled, body = shim)
    bool renderBackendIsXRMirrorEnabled(ScriptContext& ctx);
    VBIND_FN(module = RenderBackend, name = isExitRequested, body = shim)
    bool renderBackendIsExitRequested(ScriptContext& ctx);
} // namespace vultra
