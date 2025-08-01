#include "vultra/function/service/services.hpp"
#include "vultra/function/renderer/mesh_manager.hpp"
#include "vultra/function/renderer/texture_manager.hpp"

namespace vultra
{
    namespace service
    {
        void Services::init(rhi::RenderDevice& rd)
        {
            Resources::Textures::emplace(rd);
            Resources::Meshes::emplace(rd);
        }

        void Services::reset()
        {
            Resources::Meshes::reset();
            Resources::Textures::reset();
        }

        void Services::Resources::clear()
        {
            Textures::value().clear();
            Meshes::value().clear();
        }

    } // namespace service
} // namespace vultra