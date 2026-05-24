#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/structs/extent2d.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"

#include <vbase/service/service_registry.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    class Renderer;
    class RuntimeProfiler;
    class World;
    struct GaussianSplatFrameStats;
    struct GaussianSplatRenderSettings;
    struct BuiltinRenderSettings;
    struct RenderCamera;
    namespace rhi
    {
        class Texture;
    }

    struct FrameGraphDebugTexture
    {
        std::string      camera;
        std::string      name;
        std::string      key;
        std::string      resourceKey;
        rhi::Texture*    texture {nullptr};
        rhi::Extent2D    extent {};
        rhi::Extent2D    sourceExtent {};
        rhi::PixelFormat format {rhi::PixelFormat::eUndefined};
        float            zNear {0.1f};
        float            zFar {1000.0f};
    };

    struct FrameGraphTexturePreviewSettings
    {
        bool channels[4] {true, true, true, true};
        bool gammaCorrect {false};
        int  previewMode {0};
        float depthNear {0.1f};
        float depthFar {1000.0f};
        float clampMin {0.0f};
        float clampMax {1.0f};
        std::string selectedTextureKey;
    };

    class IRenderService
    {
    public:
        SERVICE_REGISTER(IRenderService)

        virtual void registerRenderer(Ref<Renderer> renderer) = 0;
        virtual std::vector<std::string> rendererKeys() const = 0;

        // Render one frame for all cooked cameras (CameraSystem output).
        virtual void renderFrame() = 0;

        // Notify render service that output size changed.
        virtual void onResize(uint32_t width, uint32_t height) = 0;
        virtual bool reloadRenderPipeline() = 0;
        virtual bool reloadRenderPipeline(std::string_view asset, std::string_view rendererKey = {}) = 0;

        // Built-in runtime profiler (default disabled).
        virtual RuntimeProfiler* runtimeProfiler() = 0;

        // DOT emitted by vrendergraph for the most recently compiled frame graph.
        virtual std::string_view lastFrameGraphSnapshot() const = 0;
        virtual void             setFrameGraphTextureCaptureEnabled(bool enabled) = 0;
        virtual bool             frameGraphTextureCaptureEnabled() const = 0;
        virtual void             setFrameGraphTexturePreviewSettings(const FrameGraphTexturePreviewSettings& settings) = 0;
        virtual FrameGraphTexturePreviewSettings frameGraphTexturePreviewSettings() const = 0;
        virtual const std::vector<FrameGraphDebugTexture>& frameGraphDebugTextures() const = 0;

        virtual GaussianSplatRenderSettings&       gaussianSplatSettings() = 0;
        virtual const GaussianSplatRenderSettings& gaussianSplatSettings() const = 0;
        virtual const GaussianSplatFrameStats&     gaussianSplatFrameStats() const = 0;
        virtual BuiltinRenderSettings&             builtinRenderSettings() = 0;
        virtual const BuiltinRenderSettings&       builtinRenderSettings() const = 0;
    };
} // namespace vultra
