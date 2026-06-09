#include "vultra/core/rhi/shader_library.hpp"
#include "vultra/core/base/common_context.hpp"

#include <vshadersystem/engine_keywords.hpp>

#include <algorithm>

namespace
{
    template<typename ShaderBinaryLike>
    std::string extractWgsl(ShaderBinaryLike& shaderBinary)
    {
        if constexpr (requires { shaderBinary.wgsl; })
        {
            return std::move(shaderBinary.wgsl);
        }
        else
        {
            return {};
        }
    }

    bool populate_shader_library_runtime(vshadersystem::ShaderLibrary&                     library,
                                         std::optional<vshadersystem::EngineKeywordsFile>& engineKeywords,
                                         bool&                                             loaded)
    {
        loaded = true;
        engineKeywords.reset();
        if (library.engineKeywordsVkw.empty())
            return true;

        std::string text(reinterpret_cast<const char*>(library.engineKeywordsVkw.data()),
                         library.engineKeywordsVkw.size());
        auto        kr = vshadersystem::parse_engine_keywords_vkw(text);
        if (kr.isOk())
        {
            engineKeywords = std::move(kr.value());
        }
        else
        {
            VULTRA_CORE_WARN("[ShaderLibraryRuntime] Failed to parse embedded engine keywords: {}", kr.error().message);
        }

        return true;
    }
} // namespace

namespace vultra
{
    namespace rhi
    {
        bool ShaderLibraryRuntime::loadFromFile(const std::string& filePath)
        {
            auto r = vshadersystem::read_vshlib_file(filePath);
            if (!r.isOk())
            {
                VULTRA_CORE_ERROR("[ShaderLibraryRuntime] Failed to read vshlib: {}", r.error().message);
                m_Loaded = false;
                return false;
            }

            m_Lib = std::move(r.value());
            return populate_shader_library_runtime(m_Lib, m_EngineKeywords, m_Loaded);
        }

        bool ShaderLibraryRuntime::loadFromMemory(const uint8_t* data, size_t size)
        {
            auto r = vshadersystem::read_vshlib(std::span<const uint8_t>(data, size));
            if (!r.isOk())
            {
                VULTRA_CORE_ERROR("[ShaderLibraryRuntime] Failed to read vshlib from memory: {}", r.error().message);
                m_Loaded = false;
                return false;
            }

            m_Lib = std::move(r.value());
            return populate_shader_library_runtime(m_Lib, m_EngineKeywords, m_Loaded);
        }

        bool ShaderLibraryRuntime::hasEngineKeywords() const { return m_EngineKeywords.has_value(); }

        const vshadersystem::EngineKeywordsFile* ShaderLibraryRuntime::engineKeywords() const
        {
            if (!m_EngineKeywords)
                return nullptr;
            return std::addressof(m_EngineKeywords.value());
        }

        bool ShaderLibraryRuntime::hasVariant(const uint64_t variantHash, const vshadersystem::ShaderStage stage) const
        {
            if (!m_Loaded)
                return false;

            return std::any_of(m_Lib.entries.begin(), m_Lib.entries.end(), [&](const auto& entry) {
                return entry.keyHash == variantHash && entry.stage == stage;
            });
        }

        std::optional<ShaderLibraryRuntime::LoadedShader>
        ShaderLibraryRuntime::load(const uint64_t variantHash, const vshadersystem::ShaderStage stage) const
        {
            if (!m_Loaded)
                return std::nullopt;

            auto br = vshadersystem::extract_vshlib_blob(m_Lib, variantHash, stage);
            if (!br.isOk())
            {
                // Diagnose the miss: a same-keyHash entry under a different stage means a
                // stage-value skew (e.g. the lib was built by a vshaderc whose ShaderStage
                // enum differs from this binary's); a same-stage-but-no-keyHash situation
                // means a variant/keyword/id-hash mismatch; neither means the entry is absent.
                size_t        sameHash = 0, sameStage = 0;
                vshadersystem::ShaderStage otherStage = vshadersystem::ShaderStage::eUnknown;
                for (const auto& e : m_Lib.entries)
                {
                    if (e.keyHash == variantHash)
                    {
                        ++sameHash;
                        otherStage = e.stage;
                    }
                    if (e.stage == stage)
                        ++sameStage;
                }
                VULTRA_CORE_WARN("[ShaderLibraryRuntime] Failed to extract vshlib blob: {} "
                                 "(variantHash={}, stage={}; entries={}, sameKeyHash={} (atStage={}), sameStage={})",
                                 br.error().message,
                                 variantHash,
                                 static_cast<int>(stage),
                                 m_Lib.entries.size(),
                                 sameHash,
                                 static_cast<int>(otherStage),
                                 sameStage);
                return std::nullopt;
            }

            auto bin = vshadersystem::read_vshbin(br.value());
            if (!bin.isOk())
            {
                VULTRA_CORE_ERROR("[ShaderLibraryRuntime] Failed to parse vshbin: {}", bin.error().message);
                return std::nullopt;
            }
            auto parsed = std::move(bin.value());

            LoadedShader out;
            out.spirv        = std::move(parsed.spirv);
            out.wgsl         = extractWgsl(parsed);
            out.materialDesc = std::move(parsed.materialDesc);
            out.shaderIdHash = parsed.shaderIdHash;
            out.variantHash  = parsed.variantHash;
            out.stage        = parsed.stage;

            out.reflection.accumulate(parsed.reflection);
            return out;
        }

        uint64_t ShaderLibraryRuntime::computeVariantHash(const std::string_view           shaderId,
                                                          const vshadersystem::ShaderStage stage,
                                                          const KeywordValues&             keywordValues)
        {
            vshadersystem::VariantKey vk;
            vk.setShaderId(shaderId);
            vk.setStage(stage);

            for (const auto& [name, value] : keywordValues)
            {
                vk.set(name, value);
            }

            return vk.build();
        }
    } // namespace rhi
} // namespace vultra
