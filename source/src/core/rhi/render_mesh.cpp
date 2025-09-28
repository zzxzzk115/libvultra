#include "vultra/core/rhi/render_mesh.hpp"
#include "vultra/core/rhi/render_device.hpp"

namespace vultra
{
    namespace rhi
    {
        void RenderMesh::createBuildBLAS(RenderDevice& rd) { blas = rd.createBuildRenderMeshBLAS(subMeshes); }
    } // namespace rhi
} // namespace vultra