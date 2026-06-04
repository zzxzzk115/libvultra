#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/structs/extent2d.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/rendering/render_structs.hpp"

#include <vbase/service/service_registry.hpp>

#include <cstdint>
#include <span>
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
    namespace rhi
    {
        class Texture;
    }

    struct FrameGraphDebugTexture
    {
        std::string      camera;
        std::string      renderer;
        std::string      name;
        std::string      key;
        std::string      resourceKey;
        std::string      transientResourceKey;
        rhi::Texture*    texture {nullptr};
        uint32_t         layer {0};
        uint32_t         layerCount {1};
        bool             imported {false};
        bool             capturable {false};
        rhi::Extent2D    extent {};
        rhi::Extent2D    sourceExtent {};
        rhi::PixelFormat format {rhi::PixelFormat::eUndefined};
        float            zNear {0.1f};
        float            zFar {1000.0f};
    };

    struct FrameGraphTexturePreviewSettings
    {
        static constexpr std::string_view kCaptureAllTextures = "__all__";
        static constexpr std::string_view kCaptureNoTextures  = "__none__";

        bool channels[4] {true, true, true, true};
        bool gammaCorrect {false};
        int  previewMode {0};
        float depthNear {0.1f};
        float depthFar {1000.0f};
        float clampMin {0.0f};
        float clampMax {1.0f};
        uint32_t maxPreviewExtent {0};
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
        virtual bool updateRenderGraph(std::string_view asset, std::string_view rendererKey = {}) = 0;
        virtual bool reloadProjectShaderLibrary(std::string_view uri) = 0;
        virtual void resetSceneState() = 0;
        virtual void releaseOverrideRenderWorld(World* world) = 0;

        // Built-in runtime profiler (default disabled).
        virtual RuntimeProfiler* runtimeProfiler() = 0;

        // JSONL debug snapshots emitted for the most recently compiled frame graphs.
        virtual std::string_view lastFrameGraphSnapshot() const = 0;
        virtual void             setFrameGraphSnapshotCaptureEnabled(bool enabled) = 0;
        virtual bool             frameGraphSnapshotCaptureEnabled() const = 0;
        virtual void             setFrameGraphTextureCaptureEnabled(bool enabled) = 0;
        virtual bool             frameGraphTextureCaptureEnabled() const = 0;
        virtual void             requestFrameGraphTextureDumpCapture(uint32_t         frames = 2,
                                                                     uint32_t         maxPreviewExtent = 0,
                                                                     std::string_view filter = {},
                                                                     std::string_view camera = {},
                                                                     std::string_view renderer = {}) = 0;
        virtual void             setFrameGraphTexturePreviewSettings(const FrameGraphTexturePreviewSettings& settings) = 0;
        virtual FrameGraphTexturePreviewSettings frameGraphTexturePreviewSettings() const = 0;
        virtual void setFrameGraphTexturePreviewOverride(std::string_view textureKey,
                                                         const FrameGraphTexturePreviewSettings& settings) = 0;
        virtual void clearFrameGraphTexturePreviewOverride(std::string_view textureKey) = 0;
        virtual void clearFrameGraphTexturePreviewOverrides() = 0;
        virtual const std::vector<FrameGraphDebugTexture>& frameGraphDebugTextures() const = 0;

        virtual GaussianSplatRenderSettings&       gaussianSplatSettings() = 0;
        virtual const GaussianSplatRenderSettings& gaussianSplatSettings() const = 0;
        virtual const GaussianSplatFrameStats&     gaussianSplatFrameStats() const = 0;
        virtual BuiltinRenderSettings&             builtinRenderSettings() = 0;
        virtual const BuiltinRenderSettings&       builtinRenderSettings() const = 0;

        // Immediate-mode debug drawing. Geometry submitted during a frame is rendered by the builtin
        // DebugDraw pass (editor scene-view cameras) and cleared automatically once the frame is done.
        // All positions are world space; colors are linear RGB in [0, 1].
        virtual void debugDrawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color) = 0;
        virtual void debugDrawAabb(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color) = 0;
        virtual void debugDrawBox(const glm::mat4& worldMatrix, const glm::vec3& halfExtents, const glm::vec3& color) = 0;
        virtual void debugDrawSphere(const glm::vec3& center, float radius, const glm::vec3& color) = 0;
        // invViewProjection maps clip space back to world; draws the camera frustum it represents.
        virtual void debugDrawFrustum(const glm::mat4& invViewProjection, const glm::vec3& color) = 0;
    };
} // namespace vultra
