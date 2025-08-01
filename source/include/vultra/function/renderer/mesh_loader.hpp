#pragma once

#include "vultra/function/renderer/mesh_resource.hpp"

namespace vultra
{
    namespace gfx
    {
        struct MeshLoader final : entt::resource_loader<MeshResource>
        {
            result_type operator()(const std::filesystem::path&, rhi::RenderDevice&);
            result_type operator()(const std::string_view, DefaultMesh&&) const;

        private:
            Ref<rhi::Texture> m_DefaultWhite1x1 {nullptr};
        };
    } // namespace gfx
} // namespace vultra