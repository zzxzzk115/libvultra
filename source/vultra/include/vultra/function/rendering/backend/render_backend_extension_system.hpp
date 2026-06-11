#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/render_backend_extension_service.hpp"

namespace vultra
{
    class RenderBackendExtensionSystem final : public EngineSubsystem, public IRenderBackendExtensionService
    {
    public:
        ENGINE_SUBSYSTEM(RenderBackendExtensionSystem)

        bool onInit() override;
        void onShutdown() override;

        bool registerExtension(IRenderBackendExtension& extension) override;
        void unregisterExtension(IRenderBackendExtension& extension) override;

        [[nodiscard]] IRenderBackendExtension* extension() const override { return m_Extension; }
        [[nodiscard]] VulkanHookTable          vulkanHooks() const override;

    private:
        IRenderBackendExtension* m_Extension {nullptr};
    };
} // namespace vultra
