#pragma once

#include "vultra/function/renderer/mesh_loader.hpp"
#include "vultra/function/renderer/mesh_resource_handle.hpp"

namespace vultra
{
    namespace gfx
    {
        using MeshCache = entt::resource_cache<MeshResource, MeshLoader>;

        class MeshManager final : public MeshCache
        {
        public:
            explicit MeshManager(rhi::RenderDevice&);
            ~MeshManager() = default;

            [[nodiscard]] MeshResourceHandle load(const std::filesystem::path&);
            void                             import(const std::string_view name, DefaultMesh&&);

        private:
            rhi::RenderDevice& m_RenderDevice;
        };
    } // namespace gfx
} // namespace vultra