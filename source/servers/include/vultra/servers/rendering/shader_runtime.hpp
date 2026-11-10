#pragma once

#include <vultra/servers/rendering/shader_material.hpp>

namespace vultra
{
    // Explicit asset ownership and whole-asset publication. No process-wide shader registry.
    class ShaderRuntime
    {
    public:
        using PrepareCandidate = std::function<void(uint32_t, ShaderMaterial&)>;
        using PrepareCandidateExpected = std::function<std::expected<void, std::string>(uint32_t, ShaderMaterial&)>;
        static std::expected<std::unique_ptr<ShaderRuntime>, std::string>
        create(Device&                                     device,
               std::filesystem::path                       source,
               ShaderCompileOptions                        options            = {},
               ShaderMaterial::TextureResolverExpected     textures           = {},
               std::vector<std::string>                    requiredLightModes = {},
               ShaderAsset::SubshaderCompatibilityExpected compatible         = {},
               const AssetSource*                          assetSource        = nullptr);
        ShaderRuntime(Device&                             device,
                      std::filesystem::path               source,
                      ShaderCompileOptions                options            = {},
                      ShaderMaterial::TextureResolver     textures           = {},
                      std::vector<std::string>            requiredLightModes = {},
                      ShaderAsset::SubshaderCompatibility compatible         = {},
                      const AssetSource*                  assetSource        = nullptr);
        ~ShaderRuntime();
        ShaderRuntime(const ShaderRuntime&)            = delete;
        ShaderRuntime& operator=(const ShaderRuntime&) = delete;

        std::expected<uint32_t, std::string>          addMaterialExpected(MaterialInstance instance = {});
        std::expected<void, std::string>              removeMaterialExpected(uint32_t index);
        std::expected<MaterialInstance*, std::string> instanceExpected(uint32_t index);
        std::expected<ShaderMaterial*, std::string>   materialExpected(uint32_t index);
        uint32_t addMaterial(MaterialInstance instance = {});
        // Detach borrowing renderer slots before removing a material. Indices are stable and reusable.
        void               removeMaterial(uint32_t index);
        const ShaderAsset& asset() const;
        MaterialInstance&  instance(uint32_t index);
        ShaderMaterial&    material(uint32_t index);
        uint64_t           generation() const;
        const std::string& diagnostics() const;
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
        bool compiling() const;
#endif

        // Call after GPU completion. The callback binds current host resources and prepares required candidate passes.
        // References obtained from instance()/material() expire on successful publication.
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
        // Host source watching/compilation is separate from cooked material ownership.
        std::expected<bool, std::string> pollExpected(const PrepareCandidateExpected& prepare = {});
        bool poll(const PrepareCandidate& prepare = {});
#endif
        std::expected<bool, std::string> reloadExpected(const PrepareCandidateExpected& prepare = {});
        bool reload(const PrepareCandidate& prepare = {});

    private:
        struct State;
        explicit ShaderRuntime(std::unique_ptr<State> state);
        std::unique_ptr<State> m_State;
    };
} // namespace vultra
