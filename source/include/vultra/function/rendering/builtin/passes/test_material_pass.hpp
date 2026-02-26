#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/rendering/srp/render_feature.hpp"

#include <optional>
#include <string_view>
#include <unordered_map>

namespace vultra
{
    namespace resource
    {
        struct GpuResourcePool;
    }

    namespace rendering
    {
        // ============================================================
        // TestMaterialPass
        //
        // CPU-driven baseline pipeline pass.
        //
        // - Vertex format is dynamic (packed AoS). This pass generates
        //   shader defines based on mesh vertex attributes.
        // - Fragment outputs a simple material color:
        //     PBRMR   -> baseColor
        //     PBRSG   -> diffuseColor
        //     Unlit   -> color
        //     Phong   -> diffuse
        //
        // This pass intentionally ignores lighting, textures, and alpha.
        // ============================================================
        class TestMaterialPass final : public RenderFeature
        {
        public:
            TestMaterialPass()  = default;
            ~TestMaterialPass() = default;

            std::string_view name() const override { return "TestMaterialPass"; }

            void addPasses(RenderContext& ctx) override;

        private:
            struct MaterialTableEntry
            {
                uint32_t model {0};
                uint32_t blockOffsetBytes {0};
                uint32_t pad0 {0};
                uint32_t pad1 {0};
            };
            static_assert(sizeof(MaterialTableEntry) == 16);

            struct PipelineState
            {
                rhi::GraphicsPipeline pipeline;
            };

            static size_t hashVertexAttributes(const rhi::VertexAttributes& attrs);
            static std::unordered_map<std::string, std::optional<std::string>>
            buildVertexDefines(const rhi::VertexAttributes& attrs);

            rhi::GraphicsPipeline& getOrCreatePipeline(rhi::RenderDevice&           rd,
                                                       const rhi::VertexAttributes& attrs,
                                                       rhi::PixelFormat             colorFormat);

            void ensureMaterialTableUploaded(rhi::RenderDevice& rd, const resource::GpuResourcePool& pool);

        private:
            std::unordered_map<size_t, PipelineState> m_Pipelines;

            Ref<rhi::StorageBuffer> m_MaterialTableBuffer {nullptr};
            size_t                  m_LastMaterialTableHash {0};
        };
    } // namespace rendering
} // namespace vultra
