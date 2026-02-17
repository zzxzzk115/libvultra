#include "vultra/function/renderer/mesh_resource.hpp"

namespace vultra
{
    namespace gfx
    {
        MeshResource::MeshResource(Mesh&& mesh, const std::filesystem::path& p) : Resource {p}, Mesh {std::move(mesh)}
        {}
    } // namespace gfx
} // namespace vultra