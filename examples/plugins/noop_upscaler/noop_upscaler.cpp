#include <vultra/core/engine/engine_context.hpp>
#include <vultra/core/plugin/engine_plugin.hpp>
#include <vultra/function/services/render_backend_extension_service.hpp>
#include <vultra/function/services/render_upscaler_service.hpp>

#include <cstdio>
#include <string_view>

namespace
{
    class NoopBackendExtension final : public vultra::IRenderBackendExtension
    {
    public:
        std::string_view name() const override { return "noop_upscaler"; }
    };

    class NoopUpscalerProvider final : public vultra::IUpscalerProvider
    {
    public:
        std::string_view name() const override { return "noop"; }

        vultra::UpscalerStatus status() const override
        {
            return {
                .available      = true,
                .activeProvider = "noop",
                .message        = "Noop upscaler provider ready",
            };
        }

        vultra::rhi::Extent2D
        queryOptimalRenderExtent(vultra::rhi::Extent2D outputExtent, vultra::UpscalerMode) override
        {
            return outputExtent;
        }

        void onResize(vultra::rhi::Extent2D outputExtent) override { m_OutputExtent = outputExtent; }

        void beginFrame(const vultra::NativeCommandContext& command) override
        {
            m_LastFrameIndex = command.frameIndex;
            m_LastViewportId = command.viewportId;
        }

        bool evaluate(const vultra::UpscalerEvaluateContext& context) override
        {
            ++m_EvaluateCount;
            m_OutputExtent = context.outputExtent;
            if (m_EvaluateCount == 1)
            {
                std::printf("[noop_upscaler] first evaluate frame=%llu viewport=%u resources=%zu\n",
                            static_cast<unsigned long long>(m_LastFrameIndex),
                            m_LastViewportId,
                            context.resources.size());
            }
            return false;
        }

        void freeResourcesForViewport(vultra::UpscalerViewportId) override {}
        void shutdown() override {}

    private:
        vultra::rhi::Extent2D m_OutputExtent {};
        uint64_t              m_LastFrameIndex {0};
        vultra::UpscalerViewportId m_LastViewportId {0};
        uint64_t              m_EvaluateCount {0};
    };

    class NoopUpscalerPlugin final : public vultra::EnginePlugin
    {
    public:
        const char* name() const override { return "noop_upscaler"; }

        bool install(vultra::EngineContext& ctx) override
        {
            auto* upscaler = ctx.services.tryGet<vultra::IRenderUpscalerService>();
            if (upscaler == nullptr)
            {
                std::fprintf(stderr, "[noop_upscaler] upscaler service unavailable\n");
                return false;
            }

            if (auto* backendExtensions = ctx.services.tryGet<vultra::IRenderBackendExtensionService>())
                backendExtensions->registerExtension(m_BackendExtension);

            const bool registered = upscaler->registerProvider(m_Provider);
            upscaler->setActiveProvider(m_Provider.name());
            upscaler->setMode(vultra::UpscalerMode::eQuality);
            std::printf("[noop_upscaler] installed early native provider\n");
            return registered;
        }

        void uninstall(vultra::EngineContext& ctx) override
        {
            if (auto* upscaler = ctx.services.tryGet<vultra::IRenderUpscalerService>())
                upscaler->unregisterProvider(m_Provider);
            if (auto* backendExtensions = ctx.services.tryGet<vultra::IRenderBackendExtensionService>())
                backendExtensions->unregisterExtension(m_BackendExtension);
            std::printf("[noop_upscaler] uninstalled\n");
        }

    private:
        NoopBackendExtension m_BackendExtension;
        NoopUpscalerProvider m_Provider;
    };
} // namespace

#if defined(_WIN32)
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

PLUGIN_EXPORT vultra::EnginePlugin* vultraCreatePlugin() { return new NoopUpscalerPlugin(); }
PLUGIN_EXPORT void                  vultraDestroyPlugin(vultra::EnginePlugin* plugin) { delete plugin; }
