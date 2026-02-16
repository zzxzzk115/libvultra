#pragma once

#include "vultra/function/resource/resource.hpp"

#include <vasset/vmesh.hpp>

namespace vultra
{
    namespace gfx
    {
        class MeshResource final : public resource::Resource
        {
        public:
            MeshResource() = default;
            explicit MeshResource(vasset::VMesh&&, const std::filesystem::path&);
            MeshResource(const MeshResource&)     = delete;
            MeshResource(MeshResource&&) noexcept = default;

            MeshResource& operator=(const MeshResource&)     = delete;
            MeshResource& operator=(MeshResource&&) noexcept = default;

        private:
            vasset::VMesh m_Mesh;
        };
    } // namespace gfx
} // namespace vultra