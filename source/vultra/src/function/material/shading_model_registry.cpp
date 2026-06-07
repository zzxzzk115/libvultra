#include "vultra/function/material/shading_model_registry.hpp"

namespace vultra::material
{
    namespace
    {
        [[nodiscard]] uint64_t hashCombine(uint64_t seed, std::string_view value)
        {
            // FNV-1a over the bytes, folded into the running seed.
            uint64_t h = 1469598103934665603ull;
            for (const char c : value)
            {
                h ^= static_cast<uint8_t>(c);
                h *= 1099511628211ull;
            }
            return seed * 1099511628211ull ^ h;
        }
    } // namespace

    uint32_t ShadingModelRegistry::registerModel(ShadingModelDesc desc)
    {
        if (auto it = m_ByName.find(desc.name); it != m_ByName.end())
            return m_Models[it->second].code; // already registered: keep its code

        desc.code          = m_NextCode++;
        const size_t index = m_Models.size();

        m_Signature = hashCombine(m_Signature, desc.name);
        m_Signature = hashCombine(m_Signature, desc.bxdfLibrary);
        m_Signature = hashCombine(m_Signature, desc.bxdfArtifact);
        m_Signature = hashCombine(m_Signature, desc.bxdfFunction);
        m_Signature ^= static_cast<uint64_t>(desc.code) << 32u;

        m_ByName.emplace(desc.name, index);
        m_ByCode.emplace(desc.code, index);
        m_Models.push_back(std::move(desc));
        return m_Models.back().code;
    }

    const ShadingModelDesc* ShadingModelRegistry::findByName(std::string_view name) const
    {
        const auto it = m_ByName.find(std::string {name});
        return it == m_ByName.end() ? nullptr : &m_Models[it->second];
    }

    const ShadingModelDesc* ShadingModelRegistry::findByCode(uint32_t code) const
    {
        const auto it = m_ByCode.find(code);
        return it == m_ByCode.end() ? nullptr : &m_Models[it->second];
    }

    void ShadingModelRegistry::clear()
    {
        m_Models.clear();
        m_ByName.clear();
        m_ByCode.clear();
        m_NextCode  = kFirstCustomShadingModelCode;
        m_Signature = 0;
    }
} // namespace vultra::material
