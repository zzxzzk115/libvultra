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

        TextureResourceHandle TextureManager::load(const std::filesystem::path& p, TextureColorSpace colorSpace)
        {
            if (colorSpace == TextureColorSpace::eAuto)
            {
                return load(p);
            }

            const auto cachePath =
                std::filesystem::path {p.generic_string() +
                                       (colorSpace == TextureColorSpace::eSRGB ? "#srgb" : "#linear")};
            auto [it, emplaced] = TextureCache::load(resource::makeResourceId(cachePath), p, m_RenderDevice, colorSpace);
            if (!it->second)
            {
                erase(it);
                return {};
            }
            if (emplaced)
            {
                VULTRA_CORE_INFO("[Resource] Loaded resource: {}", relative(cachePath).generic_string())
            }
            return it->second;
        }
    } // namespace gfx
} // namespace vultra
