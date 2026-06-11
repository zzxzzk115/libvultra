#include "vultra/function/rendering/backend/render_backend_extension_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"

namespace vultra
{
    bool RenderBackendExtensionSystem::onInit()
    {
        ctx().services.provide<IRenderBackendExtensionService>(this);
        return true;
    }

    void RenderBackendExtensionSystem::onShutdown()
    {
        if (m_Extension != nullptr)
            VULTRA_CORE_WARN("[RenderBackendExtensionSystem] Extension '{}' still registered during shutdown.",
                             m_Extension->name());
        m_Extension = nullptr;
    }

    bool RenderBackendExtensionSystem::registerExtension(IRenderBackendExtension& extension)
    {
        if (m_Extension != nullptr && m_Extension != &extension)
        {
            VULTRA_CORE_ERROR("[RenderBackendExtensionSystem] Rejecting '{}' because '{}' already owns backend hooks.",
                              extension.name(),
                              m_Extension->name());
            return false;
        }
        m_Extension = &extension;
        VULTRA_CORE_INFO("[RenderBackendExtensionSystem] Registered backend extension '{}'.", extension.name());
        return true;
    }

    void RenderBackendExtensionSystem::unregisterExtension(IRenderBackendExtension& extension)
    {
        if (m_Extension == &extension)
        {
            VULTRA_CORE_INFO("[RenderBackendExtensionSystem] Unregistered backend extension '{}'.", extension.name());
            m_Extension = nullptr;
        }
    }

    VulkanHookTable RenderBackendExtensionSystem::vulkanHooks() const
    {
        return m_Extension != nullptr ? m_Extension->vulkanHooks() : VulkanHookTable {};
    }
} // namespace vultra
