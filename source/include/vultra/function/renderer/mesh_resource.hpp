#pragma once

#include "vultra/function/renderer/default_material.hpp"
#include "vultra/function/renderer/default_vertex.hpp"
#include "vultra/function/renderer/mesh.hpp"
#include "vultra/function/resource/resource.hpp"

namespace vultra
{
    namespace gfx
    {
        using DefaultMesh = Mesh<SimpleVertex, PBRMaterial>;

        class MeshResource final : public resource::Resource, public DefaultMesh
        {
        public:
            MeshResource() = default;
            explicit MeshResource(DefaultMesh&&, const std::filesystem::path&);
            MeshResource(const MeshResource&)     = delete;
            MeshResource(MeshResource&&) noexcept = default;

            MeshResource& operator=(const MeshResource&)     = delete;
            MeshResource& operator=(MeshResource&&) noexcept = default;
        };
    } // namespace gfx
} // namespace vultra