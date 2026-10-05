#pragma once

#include <vultra/servers/rendering/shader_material.hpp>

namespace vultra
{
    // Explicit asset ownership and whole-asset publication. No process-wide shader registry.
    class ShaderRuntime
    {
    public:
        using PrepareCandidate = std::function<void(uint32_t, ShaderMaterial&)>;
        ShaderRuntime(Device&                             device,
                      std::filesystem::path               source,
                      ShaderCompileOptions                options            = {},
                      ShaderMaterial::TextureResolver     textures           = {},
                      std::vector<std::string>            requiredLightModes = {},
                      ShaderAsset::SubshaderCompatibility compatible         = {});
        ~ShaderRuntime();
        ShaderRuntime(const ShaderRuntime&)            = delete;
        ShaderRuntime& operator=(const ShaderRuntime&) = delete;

        uint32_t addMaterial(MaterialInstance instance = {});
        // Detach borrowing renderer slots before removing a material. Indices are stable and reusable.
        void               removeMaterial(uint32_t index);
        const ShaderAsset& asset() const;
        MaterialInstance&  instance(uint32_t index);
        ShaderMaterial&    material(uint32_t index);
        uint64_t           generation() const;
        const std::string& diagnostics() const;
        bool               compiling() const;

        // Call after GPU completion. The callback binds current host resources and prepares required candidate passes.
        // References obtained from instance()/material() expire on successful publication.
        bool poll(const PrepareCandidate& prepare = {});
        bool reload(const PrepareCandidate& prepare = {});

    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
} // namespace vultra
