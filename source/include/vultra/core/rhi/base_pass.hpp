#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/core/rhi/base_pipeline.hpp"
#include "vultra/core/rhi/shader_library.hpp"

#include <memory>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
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

        protected:
            RenderDevice*         m_RenderDevice {nullptr};
            ShaderLibraryRuntime* m_ShaderLib {nullptr};

        private:
            // Key = Hashed args passed to _createPipeline.
            using PipelineCache = std::unordered_map<std::size_t, Scope<PipelineType>>;
            PipelineCache m_Pipelines;
        };
    } // namespace rhi
} // namespace vultra