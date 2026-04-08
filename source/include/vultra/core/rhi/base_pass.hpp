#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/core/rhi/base_pipeline.hpp"
#include "vultra/core/rhi/shader_library.hpp"

#include <memory>
#include <typeinfo>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        enum class ShaderProfile : uint8_t
        {
            eUnspecified,
            eGeneral,
            eHighend,
            eCompatibility
        };

        class RenderDevice;

        template<class TargetPass, class PipelineType>
            requires std::is_base_of_v<BasePipeline, PipelineType>
        class BasePass
        {
        public:
            BasePass()                    = default;
            BasePass(const BasePass&)     = delete;
            BasePass(BasePass&&) noexcept = default;
            ~BasePass()                   = default;

            BasePass& operator=(const BasePass&) noexcept = delete;
            BasePass& operator=(BasePass&&) noexcept      = default;

            [[nodiscard]] auto count() const { return static_cast<uint32_t>(m_Pipelines.size()); }
            void               clear() { m_Pipelines.clear(); }

        protected:
            template<typename... Args>
            PipelineType* getPipeline(Args&&... args)
            {
                VULTRA_CORE_ASSERT(m_RenderDevice && m_ShaderLib,
                                   "RenderDevice and ShaderLib must be set before calling getPipeline");

                std::size_t hash {0};
                (hashCombine(hash, args), ...);

                if (const auto it = m_Pipelines.find(hash); it != m_Pipelines.cend())
                {
                    return it->second.get();
                }

                auto pipeline = static_cast<TargetPass*>(this)->createPipeline(std::forward<Args>(args)...);
                const auto& [inserted, _] =
                    m_Pipelines.emplace(hash, pipeline ? std::make_unique<PipelineType>(std::move(pipeline)) : nullptr);
                return inserted->second.get();
            }

            void          setRenderDevice(RenderDevice& rd) { m_RenderDevice = &rd; }
            RenderDevice& getRenderDevice() const { return *m_RenderDevice; }

            void                  setShaderLib(ShaderLibraryRuntime& shaderLib) { m_ShaderLib = &shaderLib; }
            ShaderLibraryRuntime& getShaderLib() const { return *m_ShaderLib; }

            void          setShaderProfile(const ShaderProfile profile) { m_ShaderProfile = profile; }
            ShaderProfile getShaderProfile() const { return m_ShaderProfile; }
            const char*   getShaderProfileName() const
            {
                switch (m_ShaderProfile)
                {
                    case ShaderProfile::eGeneral:
                        return "general";
                    case ShaderProfile::eHighend:
                        return "highend";
                    case ShaderProfile::eCompatibility:
                        return "compatibility";
                    default:
                        return "unspecified";
                }
            }

            void ensureShaderProfileSpecified() const
            {
                VULTRA_CORE_ASSERT(m_ShaderProfile != ShaderProfile::eUnspecified,
                                   "Shader profile must be explicitly set in pass constructor");
            }

            [[nodiscard]] uint64_t
            computeShaderVariantHash(std::string_view                          shaderId,
                                     const vshadersystem::ShaderStage          stage,
                                     const ShaderLibraryRuntime::KeywordValues keywordValues = {}) const
            {
                ensureShaderProfileSpecified();
                return getShaderLib().computeVariantHash(shaderId, stage, keywordValues);
            }

            [[nodiscard]] uint64_t
            computeGeneralVariantHash(std::string_view                          shaderId,
                                      const vshadersystem::ShaderStage          stage,
                                      const ShaderLibraryRuntime::KeywordValues keywordValues = {}) const
            {
                if (m_ShaderProfile != ShaderProfile::eGeneral)
                {
                    VULTRA_CORE_WARN("[{}] Computing general variant while pass profile is '{}'",
                                     typeid(TargetPass).name(),
                                     getShaderProfileName());
                }
                return computeShaderVariantHash(shaderId, stage, keywordValues);
            }

            [[nodiscard]] uint64_t
            computeHighendVariantHash(std::string_view                          shaderId,
                                      const vshadersystem::ShaderStage          stage,
                                      const ShaderLibraryRuntime::KeywordValues keywordValues = {}) const
            {
                if (m_ShaderProfile != ShaderProfile::eHighend)
                {
                    VULTRA_CORE_WARN("[{}] Computing highend variant while pass profile is '{}'",
                                     typeid(TargetPass).name(),
                                     getShaderProfileName());
                }
                return computeShaderVariantHash(shaderId, stage, keywordValues);
            }

            [[nodiscard]] uint64_t
            computeCompatibilityVariantHash(std::string_view                          shaderId,
                                            const vshadersystem::ShaderStage          stage,
                                            const ShaderLibraryRuntime::KeywordValues keywordValues = {}) const
            {
                if (m_ShaderProfile != ShaderProfile::eCompatibility)
                {
                    VULTRA_CORE_WARN("[{}] Computing compatibility variant while pass profile is '{}'",
                                     typeid(TargetPass).name(),
                                     getShaderProfileName());
                }
                return computeShaderVariantHash(shaderId, stage, keywordValues);
            }

            [[nodiscard]] std::optional<ShaderLibraryRuntime::LoadedShader>
            loadShader(std::string_view                          shaderId,
                       const vshadersystem::ShaderStage          stage,
                       const ShaderLibraryRuntime::KeywordValues keywordValues = {}) const
            {
                auto variantHash = computeShaderVariantHash(shaderId, stage, keywordValues);
                auto shader      = getShaderLib().load(variantHash, stage);
                if (!shader)
                {
                    VULTRA_CORE_ERROR("[{}] Failed to load shader '{}' (stage={}) variant (profile={})",
                                      typeid(TargetPass).name(),
                                      shaderId,
                                      static_cast<int>(stage),
                                      getShaderProfileName());
                }
                return shader;
            }

            [[nodiscard]] std::optional<ShaderLibraryRuntime::LoadedShader>
            loadShaderVariant(const uint64_t variantHash, const vshadersystem::ShaderStage stage) const
            {
                auto shader = getShaderLib().load(variantHash, stage);
                if (!shader)
                {
                    VULTRA_CORE_ERROR("[{}] Failed to load shader variant hash={} (stage={}, profile={})",
                                      typeid(TargetPass).name(),
                                      variantHash,
                                      static_cast<int>(stage),
                                      getShaderProfileName());
                }
                return shader;
            }

            [[nodiscard]] std::optional<ShaderLibraryRuntime::LoadedShader>
            loadGeneralShader(std::string_view                          shaderId,
                              const vshadersystem::ShaderStage          stage,
                              const ShaderLibraryRuntime::KeywordValues keywordValues = {}) const
            {
                if (m_ShaderProfile != ShaderProfile::eGeneral)
                {
                    VULTRA_CORE_WARN("[{}] Loading general shader while pass profile is '{}'",
                                     typeid(TargetPass).name(),
                                     getShaderProfileName());
                }
                return loadShader(shaderId, stage, keywordValues);
            }

            [[nodiscard]] std::optional<ShaderLibraryRuntime::LoadedShader>
            loadGeneralShaderVariant(const uint64_t variantHash, const vshadersystem::ShaderStage stage) const
            {
                if (m_ShaderProfile != ShaderProfile::eGeneral)
                {
                    VULTRA_CORE_WARN("[{}] Loading general shader variant while pass profile is '{}'",
                                     typeid(TargetPass).name(),
                                     getShaderProfileName());
                }
                return loadShaderVariant(variantHash, stage);
            }

            [[nodiscard]] std::optional<ShaderLibraryRuntime::LoadedShader>
            loadHighendShader(std::string_view                          shaderId,
                              const vshadersystem::ShaderStage          stage,
                              const ShaderLibraryRuntime::KeywordValues keywordValues = {}) const
            {
                if (m_ShaderProfile != ShaderProfile::eHighend)
                {
                    VULTRA_CORE_WARN("[{}] Loading highend shader while pass profile is '{}'",
                                     typeid(TargetPass).name(),
                                     getShaderProfileName());
                }
                return loadShader(shaderId, stage, keywordValues);
            }

            [[nodiscard]] std::optional<ShaderLibraryRuntime::LoadedShader>
            loadHighendShaderVariant(const uint64_t variantHash, const vshadersystem::ShaderStage stage) const
            {
                if (m_ShaderProfile != ShaderProfile::eHighend)
                {
                    VULTRA_CORE_WARN("[{}] Loading highend shader variant while pass profile is '{}'",
                                     typeid(TargetPass).name(),
                                     getShaderProfileName());
                }
                return loadShaderVariant(variantHash, stage);
            }

            [[nodiscard]] std::optional<ShaderLibraryRuntime::LoadedShader>
            loadCompatibilityShader(std::string_view                          shaderId,
                                    const vshadersystem::ShaderStage          stage,
                                    const ShaderLibraryRuntime::KeywordValues keywordValues = {}) const
            {
                if (m_ShaderProfile != ShaderProfile::eCompatibility)
                {
                    VULTRA_CORE_WARN("[{}] Loading compatibility shader while pass profile is '{}'",
                                     typeid(TargetPass).name(),
                                     getShaderProfileName());
                }
                return loadShader(shaderId, stage, keywordValues);
            }

            [[nodiscard]] std::optional<ShaderLibraryRuntime::LoadedShader>
            loadCompatibilityShaderVariant(const uint64_t variantHash, const vshadersystem::ShaderStage stage) const
            {
                if (m_ShaderProfile != ShaderProfile::eCompatibility)
                {
                    VULTRA_CORE_WARN("[{}] Loading compatibility shader variant while pass profile is '{}'",
                                     typeid(TargetPass).name(),
                                     getShaderProfileName());
                }
                return loadShaderVariant(variantHash, stage);
            }

        protected:
            RenderDevice*         m_RenderDevice {nullptr};
            ShaderLibraryRuntime* m_ShaderLib {nullptr};
            ShaderProfile         m_ShaderProfile {ShaderProfile::eUnspecified};

        private:
            // Key = Hashed args passed to _createPipeline.
            using PipelineCache = std::unordered_map<std::size_t, Scope<PipelineType>>;
            PipelineCache m_Pipelines;
        };
    } // namespace rhi
} // namespace vultra
