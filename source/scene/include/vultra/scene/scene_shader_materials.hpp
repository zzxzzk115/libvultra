#pragma once

#include <vultra/scene/scene_import.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>

namespace vultra
{
    // One scene consumer owns shader assets, instance bindings and imported texture views.
    // Destroy its borrowing BuiltinRenderer first. Updates run after the preceding GPU frame completes.
    class SceneShaderMaterials
    {
    public:
        SceneShaderMaterials(Device&                device,
                             const ProjectManifest& project,
                             std::filesystem::path  projectRoot,
                             const AssetSource*     source = nullptr);
        ~SceneShaderMaterials();
        SceneShaderMaterials(const SceneShaderMaterials&)            = delete;
        SceneShaderMaterials& operator=(const SceneShaderMaterials&) = delete;

        void               update(const SceneTree&                   tree,
                                  std::span<const SceneMeshInstance> instances,
                                  BuiltinRenderer&                   renderer,
                                  RenderGraph&                       graph,
                                  const BuiltinRenderer::Outputs&    outputs);
        const ShaderAsset& asset(const MaterialInstance& material);
        std::string        diagnostics() const;
        static bool        containsShaders(const SceneTree& tree);

    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
} // namespace vultra
