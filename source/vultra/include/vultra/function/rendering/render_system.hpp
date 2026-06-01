#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/function/framegraph/transient_resources.hpp"
#include "vultra/function/rendering/gpu_scene_dirty_tracker.hpp"
#include "vultra/function/rendering/framework/prepared_render_data.hpp"
#include "vultra/function/rendering/framework/render_frame_resources.hpp"
#include "vultra/function/rendering/runtime_profiler.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/renderer.hpp"
#include "vultra/function/resource/geometry_factory.hpp"
#include "vultra/function/resource/gpu_scene_database.hpp"
#include "vultra/function/resource/gpu_scene_view.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/services/world_service.hpp"

#include <string>
#include <unordered_map>
#include <optional>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class Texture;
    }

    // RenderSystem (SRP host):
    // - Reads cooked cameras from CameraSystem
    // - Resolves renderer per camera.rendererKey
    // - Builds FrameGraph per camera using RenderView + build/exec contexts
    // - Compiles & executes
    class RenderSystem final : public EngineSubsystem, public IRenderService
    {
    public:
        ENGINE_SUBSYSTEM(RenderSystem)

        bool onInit() override;
        void onShutdown() override;

        void onPreRender() override;
        void onRender() override;
        void onPostRender() override;
        void onPresent() override;

        // IRenderService
        void registerRenderer(Ref<Renderer> renderer) override;
        std::vector<std::string> rendererKeys() const override;
        void renderFrame() override;
        void onResize(uint32_t width, uint32_t height) override;
        bool reloadRenderPipeline() override;
        bool reloadRenderPipeline(std::string_view asset, std::string_view rendererKey = {}) override;
        bool updateRenderGraph(std::string_view asset, std::string_view rendererKey = {}) override;
        bool reloadProjectShaderLibrary(std::string_view uri) override;
        void resetSceneState() override;
        void releaseOverrideRenderWorld(World* world) override;

        // Optional: set default renderer key used if camera.rendererKey not found
        void setDefaultRendererKey(std::string key) { m_DefaultRendererKey = std::move(key); }

        // Optional: set fallback backbuffer target (can be used for offline rendering)
        void setBackbufferTarget(rhi::Texture* tex) { m_Backbuffer = tex; }

        // Cooked render world (read-only for renderer)
        const RenderWorld& renderWorld() const { return m_RenderWorldFront; }
        RuntimeProfiler*   runtimeProfiler() override { return &m_RuntimeProfiler; }
        std::string_view lastFrameGraphSnapshot() const override { return m_LastFrameGraphSnapshot; }
        void setFrameGraphSnapshotCaptureEnabled(bool enabled) override { m_FrameGraphSnapshotCaptureEnabled = enabled; }
        bool frameGraphSnapshotCaptureEnabled() const override { return m_FrameGraphSnapshotCaptureEnabled; }
        void setFrameGraphTextureCaptureEnabled(bool enabled) override { m_FrameGraphTextureCaptureEnabled = enabled; }
        bool frameGraphTextureCaptureEnabled() const override { return m_FrameGraphTextureCaptureEnabled; }
        void requestFrameGraphTextureDumpCapture(uint32_t         frames = 2,
                                                 uint32_t         maxPreviewExtent = 0,
                                                 std::string_view filter = {},
                                                 std::string_view camera = {},
                                                 std::string_view renderer = {}) override
        {
            m_FrameGraphTextureDumpCaptureFrames =
                std::max(m_FrameGraphTextureDumpCaptureFrames, static_cast<uint32_t>(std::clamp<int>(static_cast<int>(frames), 1, 120)));
            m_FrameGraphTextureDumpMaxPreviewExtent = maxPreviewExtent;
            m_FrameGraphTextureDumpFilter           = filter;
            m_FrameGraphTextureDumpCamera           = camera;
            m_FrameGraphTextureDumpRenderer         = renderer;
        }
        void setFrameGraphTexturePreviewSettings(const FrameGraphTexturePreviewSettings& settings) override
        {
            m_FrameGraphTexturePreviewSettings = settings;
        }
        FrameGraphTexturePreviewSettings frameGraphTexturePreviewSettings() const override
        {
            return m_FrameGraphTexturePreviewSettings;
        }
        void setFrameGraphTexturePreviewOverride(std::string_view textureKey,
                                                 const FrameGraphTexturePreviewSettings& settings) override
        {
            m_FrameGraphTexturePreviewOverrides[std::string(textureKey)] = settings;
        }
        void clearFrameGraphTexturePreviewOverride(std::string_view textureKey) override
        {
            m_FrameGraphTexturePreviewOverrides.erase(std::string(textureKey));
        }
        void clearFrameGraphTexturePreviewOverrides() override
        {
            m_FrameGraphTexturePreviewOverrides.clear();
        }
        const std::vector<FrameGraphDebugTexture>& frameGraphDebugTextures() const override
        {
            return m_FrameGraphDebugTextures;
        }
        GaussianSplatRenderSettings&       gaussianSplatSettings() override { return m_GaussianSplatSettings; }
        const GaussianSplatRenderSettings& gaussianSplatSettings() const override { return m_GaussianSplatSettings; }
        const GaussianSplatFrameStats&     gaussianSplatFrameStats() const override { return m_GaussianSplatStats; }
        BuiltinRenderSettings&             builtinRenderSettings() override { return m_BuiltinRenderSettings; }
        const BuiltinRenderSettings&       builtinRenderSettings() const override { return m_BuiltinRenderSettings; }

    private:
        Ref<Renderer> resolveRenderer(const RenderCamera& cam) const;
        bool          rendererRequiresRayTracingScene(std::string_view rendererKey) const;
        bool          reloadRenderPipelineNow();
        bool          reloadRenderPipelineNow(std::string_view asset, std::string_view rendererKey);
        bool          updateRenderGraphNow(std::string_view asset, std::string_view rendererKey);
        void          clearFrameGraphDebugState();
        void          addFrameGraphTextureCapturePasses(FrameGraphBuildContext& ctx, const RenderCamera& camera);
        rhi::GraphicsPipeline* getFrameGraphTexturePreviewPipeline(rhi::RenderDevice& rd,
                                                                   rhi::ShaderLibraryRuntime& shaderLib,
                                                                   rhi::PixelFormat colorFormat);

    private:
        bool m_SkipRender {false};
        bool m_Initialized {false};
        bool m_InRenderFrame {false};
        bool m_PendingRenderPipelineReload {false};
        std::string m_PendingRenderPipelineAsset;
        std::string m_PendingRenderPipelineRendererKey;
        bool        m_PendingRenderGraphUpdate {false};
        std::string m_PendingRenderGraphUpdateAsset;
        std::string m_PendingRenderGraphUpdateRendererKey;

        std::unordered_map<std::string, Ref<Renderer>> m_Renderers;
        std::string                                    m_DefaultRendererKey {"builtin"};

        rhi::Texture*                                   m_Backbuffer {nullptr};
        std::unique_ptr<framegraph::TransientResources> m_TransientResources {nullptr};

        RenderWorld m_RenderWorldFront {};
        RenderWorld m_RenderWorldBack {};

        resource::GpuSceneDatabase m_GpuSceneDatabaseFront {};
        resource::GpuSceneDatabase m_GpuSceneDatabaseBack {};

        resource::GpuSceneView m_GpuSceneViewFront {};
        resource::GpuSceneView m_GpuSceneViewBack {};

        uint64_t m_FrameCounter {0};

        RenderFrameResources m_FrameResources {};
        FrameRenderData      m_PreparedFrameData {};

        Samplers m_Samplers;

        bool                 m_EnableGpuDrivenMeshletPipeline {true};
        GpuSceneDirtyTracker m_GpuSceneDirtyTracker;
        RuntimeProfiler      m_RuntimeProfiler;
        std::string          m_LastFrameGraphSnapshot;
        struct FrameGraphDebugTextureSlot
        {
            std::optional<rhi::Texture> texture;
            std::string                 camera;
            std::string                 name;
            std::string                 key;
            rhi::Extent2D               extent {};
            rhi::PixelFormat            format {rhi::PixelFormat::eUndefined};
            uint64_t                    lastTouchedFrame {0};
        };
        std::vector<FrameGraphDebugTextureSlot> m_RetiredFrameGraphDebugTextureSlots;
        bool                                              m_FrameGraphSnapshotCaptureEnabled {false};
        bool                                              m_FrameGraphTextureCaptureEnabled {false};
        uint32_t                                          m_FrameGraphTextureDumpCaptureFrames {0};
        uint32_t                                          m_FrameGraphTextureDumpMaxPreviewExtent {0};
        std::string                                       m_FrameGraphTextureDumpFilter;
        std::string                                       m_FrameGraphTextureDumpCamera;
        std::string                                       m_FrameGraphTextureDumpRenderer;
        FrameGraphTexturePreviewSettings                  m_FrameGraphTexturePreviewSettings;
        std::unordered_map<std::string, FrameGraphTexturePreviewSettings> m_FrameGraphTexturePreviewOverrides;
        std::optional<rhi::GraphicsPipeline>              m_FrameGraphTexturePreviewPipeline;
        rhi::PixelFormat                                  m_FrameGraphTexturePreviewPipelineFormat {
            rhi::PixelFormat::eUndefined};
        std::unordered_map<std::string, FrameGraphDebugTextureSlot> m_FrameGraphDebugTextureSlots;
        std::vector<FrameGraphDebugTexture>               m_FrameGraphDebugTextures;
        GaussianSplatRenderSettings m_GaussianSplatSettings;
        GaussianSplatRenderSettings m_AppliedGaussianSplatSettings;
        GaussianSplatFrameStats     m_GaussianSplatStats;
        BuiltinRenderSettings       m_BuiltinRenderSettings;
        GeometryFactory             m_GeometryFactory;

        struct OverrideRenderWorldSlot
        {
            World*                       world {nullptr};
            RenderWorld                  renderWorld;
            resource::GpuSceneDatabase   gpuSceneDatabase;
            resource::GpuSceneView       gpuSceneView;
            uint64_t                     lastTouchedFrame {0};
        };
        std::vector<OverrideRenderWorldSlot> m_OverrideRenderWorlds;
    };

    // Cook World into RenderWorld.
    class RenderWorldCooker
    {
    public:
        static void cook(World&              world,
                         IAssetService&      assets,
                         IGpuResourceService& gpuResources,
                         rhi::RenderDevice&  rd,
                         GeometryFactory&    geometryFactory,
                         RenderWorld&        out,
                         float               timeSeconds = 0.0f);
    };
} // namespace vultra
