#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace vultra::material
{
    // Builtin shading-model codes (must match the deferred lighting shader and
    // direct_gbuffer_pass graphShadingModelCode): PBRMR=1, PBRSG=2, Unlit=3,
    // Phong=4, ToonLike=6. Custom (registered) model codes start past them so
    // they never collide with builtin codes.
    inline constexpr uint32_t kFirstCustomShadingModelCode = 8u;

    // A user-defined shading model. The BXDF is authored as a GLSL artifact in a
    // shader library and referenced BY NAME (never embedded as a string), so the
    // same model works in any lighting path (deferred now, forward+ later).
    struct ShadingModelDesc
    {
        std::string name;

        // GLSL artifact reference resolved against a shader library / asset root.
        std::string bxdfLibrary {"project"};                  // "builtin" or a project library name
        std::string bxdfArtifact;                             // include path, e.g. "bxdf/iridescent.glsl"
        std::string bxdfFunction;                             // entry fn, e.g. "vultra_bxdf_iridescent"

        // Optional per-model (not per-instance) extra params, uploaded to
        // ShadingModelParamsBuffer[code] and fetched inside the BXDF.
        uint32_t       extraParamSize {0};
        nlohmann::json defaultExtraParams;

        // Assigned by the registry on registration.
        uint32_t code {0};
    };

    // Path-agnostic registry of custom shading models. One instance is owned by
    // the render service; populated when the render pipeline asset loads.
    class ShadingModelRegistry
    {
    public:
        // Registers (or returns the existing code for) a model. Codes are assigned
        // from kFirstCustomShadingModelCode upward and never reused.
        uint32_t registerModel(ShadingModelDesc desc);

        [[nodiscard]] const ShadingModelDesc* findByName(std::string_view name) const;
        [[nodiscard]] const ShadingModelDesc* findByCode(uint32_t code) const;
        [[nodiscard]] const std::vector<ShadingModelDesc>& all() const { return m_Models; }
        [[nodiscard]] bool                                 empty() const { return m_Models.empty(); }

        void clear();

        // Stable hash of the registered set; used to invalidate runtime-compiled
        // lighting variants only when the set actually changes.
        [[nodiscard]] uint64_t signature() const { return m_Signature; }

    private:
        std::vector<ShadingModelDesc>           m_Models;
        std::unordered_map<std::string, size_t> m_ByName;
        std::unordered_map<uint32_t, size_t>    m_ByCode;
        uint32_t                                m_NextCode {kFirstCustomShadingModelCode};
        uint64_t                                m_Signature {0};
    };
} // namespace vultra::material
