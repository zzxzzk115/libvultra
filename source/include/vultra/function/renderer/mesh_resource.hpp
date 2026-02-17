#pragma once

#include "vultra/function/renderer/mesh.hpp"
#include "vultra/function/resource/resource.hpp"

namespace vultra
{
    namespace gfx
    {
        class MeshResource final : public resource::Resource, public Mesh
        {
        public:
            MeshResource() = default;
            explicit MeshResource(Mesh&&, const std::filesystem::path&);
            MeshResource(const MeshResource&)     = delete;
            MeshResource(MeshResource&&) noexcept = default;

            MeshResource& operator=(const MeshResource&)     = delete;
            MeshResource& operator=(MeshResource&&) noexcept = default;
        };
    } // namespace gfx
} // namespace vultra