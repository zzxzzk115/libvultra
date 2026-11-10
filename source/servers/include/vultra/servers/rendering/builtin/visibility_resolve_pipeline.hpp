#pragma once

#include <vultra/drivers/rhi/shader_pipeline.hpp>

namespace vultra
{
    class VisibilityColorInput;

    enum class VisibilityResolveMode
    {
        eAll,
        eHoles,
        eDemand
    };

    // Indexed visibility resolve pipeline. Descriptors and graph passes are supplied by the caller.
    class VisibilityResolvePipeline
    {
    public:
        static std::expected<std::unique_ptr<VisibilityResolvePipeline>, std::string>
        create(Device&                     device,
               const ShaderProgram&        program,
               size_t                      materialCount,
               uint64_t                    vertexBufferSize,
               VisibilityResolveMode       mode               = VisibilityResolveMode::eAll,
               bool                        clustered          = false,
               const ShaderProgram*        uncoatedProgram    = nullptr,
               const ShaderProgram*        sceneLightsProgram = nullptr,
               const VisibilityColorInput* colorInput         = nullptr);
        ~VisibilityResolvePipeline();
        VisibilityResolvePipeline(const VisibilityResolvePipeline&)            = delete;
        VisibilityResolvePipeline& operator=(const VisibilityResolvePipeline&) = delete;

        VriPipelineLayout* layout() const
        {
            return m_Layout;
        }

        VriPipeline* handle() const
        {
            return m_Pipeline ? m_Pipeline->handle() : nullptr;
        }

        // Optional program with VULTRA_OPENPBR_UNCOATED_SURFACE; bindings select it from uploaded materials.
        VriPipeline* uncoatedHandle() const
        {
            return m_UncoatedPipeline ? m_UncoatedPipeline->handle() : nullptr;
        }

        // Zero coat and usesOnlySceneLights() for the uploaded frame; otherwise retain the preceding variants.
        VriPipeline* sceneLightsHandle() const
        {
            return m_SceneLightsPipeline ? m_SceneLightsPipeline->handle() : nullptr;
        }

        size_t materialCount() const
        {
            return m_MaterialCount;
        }

        bool batched() const
        {
            return m_Batched;
        }

        VisibilityResolveMode mode() const
        {
            return m_Mode;
        }

        const VisibilityColorInput* colorInput() const
        {
            return m_ColorInput;
        }

        bool clustered() const
        {
            return m_Clustered;
        }

        uint64_t vertexBufferSize() const
        {
            return m_VertexBufferSize;
        }

        uint64_t vertexPageSize() const
        {
            return m_VertexPageSize;
        }

        uint32_t vertexPageCount() const
        {
            return m_VertexPageCount;
        }

        bool belongsTo(const Device& device) const
        {
            return &device == &m_Device;
        }

    private:
        explicit VisibilityResolvePipeline(Device& device) :
            m_Device(device)
        {
        }

        const VisibilityColorInput*     m_ColorInput = nullptr;
        Device&                         m_Device;
        VriPipelineLayout*              m_Layout           = nullptr;
        size_t                          m_MaterialCount    = 0;
        uint64_t                        m_VertexBufferSize = 0;
        uint64_t                        m_VertexPageSize   = 0;
        uint32_t                        m_VertexPageCount  = 0;
        bool                            m_Batched          = false;
        bool                            m_Clustered        = false;
        VisibilityResolveMode           m_Mode             = VisibilityResolveMode::eAll;
        std::unique_ptr<ShaderPipeline> m_Pipeline;
        std::unique_ptr<ShaderPipeline> m_UncoatedPipeline;
        std::unique_ptr<ShaderPipeline> m_SceneLightsPipeline;
    };
} // namespace vultra
