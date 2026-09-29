#pragma once
#include <vultra/core/rhi/resources.hpp>

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace vultra
{
    class Profiler;
    enum class Usage
    {
        eColorWrite,
        eColorReadWrite,
        eDepthWrite,
        eDepthRead,
        eDepthReadWrite,
        eSampled,
        eStorageRead,
        eStorageWrite,
        eStorageReadWrite,
        eCopySource,
        eCopyDestination,
        ePresent
    };

    class RenderGraph
    {
    public:
        struct Resource
        {
            const RenderGraph* graph;
            uint32_t           index;
        };

        struct Use
        {
            Resource resource;
            Usage    usage;
        };

        using ExecutePass = std::function<void(VriCommandBuffer*, RenderGraph&)>;

        explicit RenderGraph(Device& device) :
            m_Device(device)
        {
        }

        RenderGraph(const RenderGraph&)            = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;
        Resource     importResource(std::string name, Texture& texture, bool initialized = true);
        Resource     importResource(std::string name, Buffer& buffer, bool initialized = true);
        Resource     createTexture(std::string name, const VriTextureDesc& desc);
        Resource     createBuffer(std::string name, const VriBufferDesc& desc);
        void addPass(std::string name, std::initializer_list<Use> uses, ExecutePass execute, bool sideEffect = false);
        void exportResource(Resource resource);
        void compile();
        void execute(VriCommandBuffer* cmd, Profiler* profiler = nullptr);
        // Rebind a same-format/size imported backbuffer after acquisition, before execute.
        void                     bind(Resource resource, Texture& texture);
        Texture&                 getTexture(Resource resource);
        Buffer&                  getBuffer(Resource resource);
        std::vector<std::string> activePasses() const;

    private:
        struct Entry
        {
            std::string              name;
            bool                     isTexture   = true;
            bool                     imported    = false;
            bool                     initialized = false;
            VriTextureDesc           textureDesc {};
            VriBufferDesc            bufferDesc {};
            Texture*                 texture = nullptr;
            Buffer*                  buffer  = nullptr;
            std::unique_ptr<Texture> ownedTexture;
            std::unique_ptr<Buffer>  ownedBuffer;
        };

        struct Pass
        {
            std::string         name;
            std::vector<Use>    uses;
            ExecutePass         execute;
            bool                sideEffect = false;
            bool                live       = false;
            std::vector<size_t> dependencies;
        };

        Entry&                lookup(Resource resource);
        Device&               m_Device;
        std::vector<Entry>    m_Resources;
        std::vector<Pass>     m_Passes;
        std::vector<Resource> m_Exports;
        bool                  m_Compiled = false;
    };
} // namespace vultra
