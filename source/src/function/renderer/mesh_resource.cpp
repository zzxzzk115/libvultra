#include "vultra/function/renderer/mesh_resource.hpp"

namespace vultra
{
    namespace gfx
    {
        MeshResource::MeshResource(vasset::VMesh&& mesh, const std::filesystem::path& p) :
            Resource {p}, m_Mesh {std::move(mesh)}
        {}
    } // namespace gfx
} // namespace vultra