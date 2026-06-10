#pragma once

namespace vbase
{
    class ServiceRegistry;
}

namespace vultra
{
    struct FrameGraphBuildContext;
    class DeclarativeRenderer;
    class IRenderService;

    // Bridges a builtin pass's build() body to the owning DeclarativeRenderer's
    // per-frame state. Reads the LIVE current build context and services (never a
    // registration-time snapshot), so a build() captured at registration still sees
    // the right frame. Friend of DeclarativeRenderer (see declarative_renderer.hpp).
    class BuiltinPassHost
    {
    public:
        explicit BuiltinPassHost(DeclarativeRenderer& owner) : m_Owner(owner) {}

        // Live per-frame build context; null outside DeclarativeRenderer's graph build.
        [[nodiscard]] FrameGraphBuildContext* currentBuildContext() const;
        [[nodiscard]] vbase::ServiceRegistry* services() const;
        [[nodiscard]] IRenderService*         renderService() const;

        // Whether tone mapping should still be applied this frame; debug view modes
        // clear it. DeferredLighting clears it, ToneMapping reads it.
        [[nodiscard]] bool applyToneMappingThisFrame() const;
        void               setApplyToneMappingThisFrame(bool value);

    private:
        DeclarativeRenderer& m_Owner;
    };
} // namespace vultra
