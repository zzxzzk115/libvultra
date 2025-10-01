#include "vultra/core/rhi/render_mesh.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include <glm/mat4x4.hpp>

namespace vultra
{
    namespace rhi
    {
        void RenderMesh::createBuildAccelerationStructures(RenderDevice& rd, const glm::mat4& transform)
        {
            blas = rd.createBuildRenderMeshBLAS(subMeshes);
            tlas = rd.createBuildTLAS(blas, transform);
        }

        void RenderMesh::updateTLAS(RenderDevice& rd, const glm::mat4& transform)
        {
            // TODO: Update TLAS
        }
    } // namespace rhi
} // namespace vultra