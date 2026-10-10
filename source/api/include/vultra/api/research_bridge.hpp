#pragma once

#include <vultra/api/research_api.h>
#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/graph/pass_catalog.hpp>

namespace vultra
{
    // Destroy project graphs before unloading modules, and modules before destroying this bridge.
    class ResearchBridge
    {
    public:
        ResearchBridge(Device&                device,
                       const ProjectManifest& project,
                       const AssetSource&     source,
                       std::filesystem::path  sdkDirectory = {});
        ~ResearchBridge();
        ResearchBridge(const ResearchBridge&)                                = delete;
        ResearchBridge&                     operator=(const ResearchBridge&) = delete;
        const VultraResearchApi&            api() const;
        PassCatalog&                        catalog();
        void                                finishRegistration();
        void                                poll();
        std::string                         diagnostics() const;
        std::vector<ShaderPipelineIdentity> shaderIdentities() const;
        void                                setFrame(const VultraResearchFrame& frame);
        void                                setEditor(const VultraResearchEditorApi* editor);
        bool                                hasEditorControls() const;

    private:
        class NativePass;
        struct Scope;
        static ResearchBridge&       access(VultraGraphFrame frame);
        static RenderGraph::Resource resource(VultraGraphResource value);
        static VultraGraphResource   resource(RenderGraph::Resource value);
        void                         registerPass(const VultraNativePassDefinition& definition);
        ShaderPipeline&              shader(void* handle);

        Device&                                      m_Device;
        const ProjectManifest&                       m_Project;
        const AssetSource&                           m_Source;
        std::filesystem::path                        m_ShaderRoot;
        PassCatalog                                  m_Catalog;
        VultraResearchApi                            m_Api {};
        VultraResearchFrame                          m_Frame {};
        RenderGraph*                                 m_ActiveGraph = nullptr;
        uint64_t                                     m_Serial      = 0;
        bool                                         m_Executing   = false;
        bool                                         m_Registering = true;
        std::string                                  m_EditorName;
        std::vector<std::unique_ptr<ShaderPipeline>> m_Shaders;
    };
} // namespace vultra
