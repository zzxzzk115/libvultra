#include "vultra/function/renderer/mesh_manager.hpp"
#include "vultra/core/rhi/render_device.hpp"

namespace vultra
{
    namespace gfx
    {
        MeshLoadingSettings MeshManager::s_GlobalLoadingSettings {};

        MeshManager::MeshManager(rhi::RenderDevice& rd) : m_RenderDevice(rd) {}

        MeshResourceHandle MeshManager::load(const std::filesystem::path& p)
        {
            return resource::load(*this, p, m_RenderDevice);
        }

        void MeshManager::import(const std::string_view name, DefaultMesh&& mesh)
        {
            MeshCache::load(entt::hashed_string {name.data()}.value(), name, std::move(mesh));
        }

        void MeshManager::setGlobalLoadingSettings(const MeshLoadingSettings& settings)
        {
            s_GlobalLoadingSettings = settings;
        }

        const MeshLoadingSettings& MeshManager::getGlobalLoadingSettings() { return s_GlobalLoadingSettings; }
    } // namespace gfx
} // namespace vultra