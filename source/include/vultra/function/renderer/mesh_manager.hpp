#pragma once

#include "vultra/function/renderer/mesh_loader.hpp"
#include "vultra/function/renderer/mesh_resource_handle.hpp"

namespace vultra
{
    namespace gfx
    {
        using MeshCache = entt::resource_cache<MeshResource, MeshLoader>;

        struct MeshLoadingSettings
        {
            bool useOptimizedMesh     = false;
            bool useOptimizedTextures = false;
        };

        class MeshManager final : public MeshCache
        {
        public:
            explicit MeshManager(rhi::RenderDevice&);
            ~MeshManager() = default;

            [[nodiscard]] MeshResourceHandle load(const std::filesystem::path&);
            void                             import(const std::string_view name, DefaultMesh&&);

            static void                       setGlobalLoadingSettings(const MeshLoadingSettings& settings);
            static const MeshLoadingSettings& getGlobalLoadingSettings();

        private:
            rhi::RenderDevice& m_RenderDevice;

            static MeshLoadingSettings s_GlobalLoadingSettings;
        };
    } // namespace gfx
} // namespace vultra