#include "vultra/core/rhi/shader_library.hpp"
#include "vultra/core/base/common_context.hpp"

#include <vshadersystem/engine_keywords.hpp>

#include <fstream>

namespace
{
    bool write_all(std::ofstream& f, const void* data, size_t size)
    {
        f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        return f.good();
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

            m_Lib    = std::move(r.value());
            m_Loaded = true;

            m_EngineKeywords.reset();
            if (!m_Lib.engineKeywordsVkw.empty())
            {
                std::string text(reinterpret_cast<const char*>(m_Lib.engineKeywordsVkw.data()),
                                 m_Lib.engineKeywordsVkw.size());
                auto        kr = vshadersystem::parse_engine_keywords_vkw(text);
                if (kr.isOk())
                {
                    m_EngineKeywords = std::move(kr.value());
                }
                else
                {
                    VULTRA_CORE_WARN("[ShaderLibraryRuntime] Failed to parse embedded engine keywords: {}",
                                     kr.error().message);
                }
            }

            return true;
        }

        bool ShaderLibraryRuntime::loadFromMemory(const uint8_t* data, size_t size)
        {
            const std::string tempFilePath = "temp_vshlib.bin";
            {
                std::ofstream f(tempFilePath, std::ios::binary);
                if (!f)
                    return false;

                auto r = write_all(f, data, size);
                if (!r)
                    return false;
            }

            auto result = loadFromFile(tempFilePath);

            // Clean up the temp file.
            std::filesystem::remove(tempFilePath);

            return result;
        }

        bool ShaderLibraryRuntime::hasEngineKeywords() const { return m_EngineKeywords.has_value(); }

        const vshadersystem::EngineKeywordsFile* ShaderLibraryRuntime::engineKeywords() const
        {
            if (!m_EngineKeywords)
                return nullptr;
            return std::addressof(m_EngineKeywords.value());
        }

        std::optional<ShaderLibraryRuntime::LoadedShader>
        ShaderLibraryRuntime::load(const uint64_t variantHash, const vshadersystem::ShaderStage stage) const
        {
            if (!m_Loaded)
                return std::nullopt;

            auto br = vshadersystem::extract_vshlib_blob(m_Lib, variantHash, stage);
            if (!br.isOk())
            {
                VULTRA_CORE_WARN("[ShaderLibraryRuntime] Failed to extract vshlib blob: {}", br.error().message);
                return std::nullopt;
            }

            auto bin = vshadersystem::read_vshbin(br.value());
            if (!bin.isOk())
            {
                VULTRA_CORE_ERROR("[ShaderLibraryRuntime] Failed to parse vshbin: {}", bin.error().message);
                return std::nullopt;
            }

            LoadedShader out;
            out.spirv        = std::move(bin.value().spirv);
            out.materialDesc = std::move(bin.value().materialDesc);
            out.shaderIdHash = bin.value().shaderIdHash;
            out.variantHash  = bin.value().variantHash;
            out.stage        = bin.value().stage;

            out.reflection.accumulate(bin.value().reflection);
            return out;
        }

        uint64_t
        ShaderLibraryRuntime::computeVariantHash(const std::string_view                           shaderId,
                                                 const vshadersystem::ShaderStage                 stage,
                                                 const std::unordered_map<std::string, uint32_t>& keywordValues)
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