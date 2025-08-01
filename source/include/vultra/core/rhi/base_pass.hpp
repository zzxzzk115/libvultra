#pragma once

#include "vultra/core/base/hash.hpp"
#include "vultra/core/rhi/base_pipeline.hpp"

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
            explicit BasePass(RenderDevice& rd) : m_RenderDevice {rd} {}
            BasePass(const BasePass&)     = delete;
            BasePass(BasePass&&) noexcept = default;
            ~BasePass()                   = default;

            BasePass& operator=(const BasePass&) noexcept = delete;
            BasePass& operator=(BasePass&&) noexcept      = default;

            RenderDevice& getRenderDevice() const { return m_RenderDevice; }

            [[nodiscard]] auto count() const { return static_cast<uint32_t>(m_Pipelines.size()); }
            void               clear() { m_Pipelines.clear(); }

        protected:
            template<typename... Args>
            PipelineType* getPipeline(Args&&... args)
            {
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

        private:
            RenderDevice& m_RenderDevice;

            // Key = Hashed args passed to _createPipeline.
            using PipelineCache = std::unordered_map<std::size_t, std::unique_ptr<PipelineType>>;
            PipelineCache m_Pipelines;
        };
    } // namespace rhi
} // namespace vultra