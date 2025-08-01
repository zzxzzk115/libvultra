#include "vultra/function/renderer/texture_manager.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/resource/resource.hpp"

namespace vultra
{
    namespace gfx
    {
        TextureManager::TextureManager(rhi::RenderDevice& rd) : m_RenderDevice {rd} {}

        TextureResourceHandle TextureManager::load(const std::filesystem::path& p)
        {
            return resource::load(*this, p, m_RenderDevice);
        }
    } // namespace gfx
} // namespace vultra