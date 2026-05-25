#pragma once

#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"

#include <cstdint>
#include <vshadersystem/binary.hpp>
#include <vshadersystem/engine_keywords.hpp>
#include <vshadersystem/library.hpp>
#include <vshadersystem/variant_key.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        // ------------------------------------------------------------
        // Runtime .vshlib loader
        //
        // This is a thin adapter that lets vultra runtime:
        // - load a cooked shader library (.vshlib)
        // - fetch a compiled variant by (variantHash, stage)
        // - get SPIR-V, material description, and reflection (converted to vultra::rhi layout)
        //
        // Keyword / permutation resolution is done by the cook step.
        // At runtime, typically compute the variantHash via vshadersystem::VariantKey.
        // ------------------------------------------------------------
        class ShaderLibraryRuntime final
        {
        public:
            using KeywordValues = std::unordered_map<std::string, uint32_t>;

            struct LoadedShader
            {
                rhi::SPIRV                         spirv;
                std::string                        wgsl;
                rhi::ShaderReflection              reflection;
                vshadersystem::MaterialDescription materialDesc;

                uint64_t shaderIdHash = 0;
                uint64_t variantHash  = 0;

                vshadersystem::ShaderStage stage = vshadersystem::ShaderStage::eUnknown;
            };

            ShaderLibraryRuntime()  = default;
            ~ShaderLibraryRuntime() = default;

            bool loadFromFile(const std::string& filePath);
            bool loadFromMemory(const uint8_t* data, size_t size);

            [[nodiscard]] bool                                     hasEngineKeywords() const;
            [[nodiscard]] const vshadersystem::EngineKeywordsFile* engineKeywords() const;

            [[nodiscard]] bool hasVariant(uint64_t variantHash, vshadersystem::ShaderStage stage) const;

            // Fetch a shader by cooked key.
            [[nodiscard]] std::optional<LoadedShader> load(uint64_t                   variantHash,
                                                           vshadersystem::ShaderStage stage) const;

            // Convenience: compute variantHash from (shaderId, stage, keyword values).
            // keywordValues: NAME -> value.
            [[nodiscard]] static uint64_t
            computeVariantHash(std::string_view                                 shaderId,
                               vshadersystem::ShaderStage                       stage,
                               const KeywordValues&                             keywordValues);

        private:
            vshadersystem::ShaderLibrary                     m_Lib;
            std::optional<vshadersystem::EngineKeywordsFile> m_EngineKeywords;
            bool                                             m_Loaded = false;
        };
    } // namespace rhi
} // namespace vultra
