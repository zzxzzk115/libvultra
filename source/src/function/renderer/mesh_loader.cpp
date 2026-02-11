#include "vultra/function/renderer/mesh_loader.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/function/resource/raw_resource_loader.hpp"

namespace vultra
{
    namespace gfx
    {
        std::expected<gfx::DefaultMesh, std::string> tryLoad(const std::filesystem::path& p, rhi::RenderDevice& rd)
        {
            if (p.extension() == ".vmesh")
            {
                return resource::loadMesh_VMesh(p, rd);
            }
            else
            {
                return resource::loadMesh_Raw(p, rd);
            }
        }

        entt::resource_loader<MeshResource>::result_type MeshLoader::operator()(const std::filesystem::path& p,
                                                                                rhi::RenderDevice&           rd)
        {
            if (m_DefaultWhite1x1 == nullptr)
            {
                m_DefaultWhite1x1 = createDefaultTexture(255, 255, 255, 255, rd);
                rd.addLoadedTexture(m_DefaultWhite1x1); // Ensure the index 0 is always white 1x1 as fallback
            }

            if (auto mesh = tryLoad(p, rd); mesh)
            {
                return createRef<MeshResource>(std::move(mesh.value()), p);
            }
            else
            {
                VULTRA_CORE_ERROR("[MeshLoader] Mesh loading failed. {}", mesh.error());
                return {};
            }
        }

        entt::resource_loader<MeshResource>::result_type MeshLoader::operator()(const std::string_view name,
                                                                                DefaultMesh&&          mesh) const
        {
            return createRef<MeshResource>(std::move(mesh), name);
        }
    } // namespace gfx
} // namespace vultra