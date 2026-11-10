#include <vultra/servers/rendering/builtin/shadow_graph.hpp>

#include <algorithm>
#include <set>

namespace vultra
{
    std::expected<void, std::string> addShadowRasterGraphPasses(RenderGraph&                graph,
                                                                const ShadowRasterPipeline& pipeline,
                                                                std::string                 name,
                                                                ShadowGraphInputs           inputs)
    {
        auto& device = graph.device();
        if (!pipeline.belongsTo(device) || !inputs.frameSet || inputs.materialSets.empty())
        {
            return std::unexpected("Invalid shadow graph pipeline or descriptors");
        }
        const std::array buffers {inputs.vertices, inputs.indices, inputs.transforms, inputs.frame};
        for (auto resource : buffers)
        {
            if (!graph.ownsResource(resource) || !graph.getBufferExpected(resource))
            {
                return std::unexpected("Shadow graph requires imported buffers");
            }
        }
        std::set<uint32_t> unique;
        for (auto resource : inputs.depths)
        {
            if (!graph.ownsResource(resource) || !unique.insert(resource.index).second)
            {
                return std::unexpected("Shadow graph requires four distinct depth resources");
            }
            const auto  info = graph.resourceInfo(resource);
            const auto& desc = info.textureDesc;
            if (!info.isTexture || desc.format != VriFormat_D32_SFLOAT || desc.type != VriTextureType_2D ||
                !desc.width || !desc.height || desc.depth != 1 || desc.layerNum != 1 || desc.mipNum != 1 ||
                desc.sampleNum != 1 || !(desc.usage & VriTextureUsage_DepthStencilAttachment) ||
                !(desc.usage & VriTextureUsage_ShaderResource))
            {
                return std::unexpected("Shadow graph requires sampleable single-view D32 depth resources");
            }
        }
        for (auto resource : inputs.materialTextures)
        {
            if (!graph.ownsResource(resource) || !graph.resourceInfo(resource).isTexture ||
                !unique.insert(resource.index).second)
            {
                return std::unexpected("Shadow material textures must be unique graph textures");
            }
        }
        for (size_t cascade = 0; cascade < inputs.depths.size(); ++cascade)
        {
            std::vector<RenderGraph::Use> uses {
                {inputs.depths[cascade], Usage::eDepthWrite, inputs.redraw[cascade] == nullptr},
                {inputs.vertices, Usage::eStorageRead},
                {inputs.indices, Usage::eStorageRead},
                {inputs.transforms, Usage::eStorageRead},
                {inputs.frame, Usage::eConstantRead}};
            for (auto texture : inputs.materialTextures)
            {
                uses.push_back({texture, Usage::eSampled});
            }
            auto added = graph.addPassExpected(
                name + "." + std::to_string(cascade),
                uses,
                [&device,
                 &pipeline,
                 depthResource  = inputs.depths[cascade],
                 vertexResource = inputs.vertices,
                 indexResource  = inputs.indices,
                 frameSet       = inputs.frameSet,
                 materialSets   = inputs.materialSets,
                 draws          = std::move(inputs.draws[cascade]),
                 dynamicDraws   = inputs.dynamicDraws[cascade],
                 redraw         = inputs.redraw[cascade],
                 cascade](VriCommandBuffer* cmd, RenderGraph& active) -> std::expected<void, std::string>
                {
                    if (redraw && !*redraw)
                    {
                        return {};
                    }
                    auto depth    = active.getTextureExpected(depthResource);
                    auto vertices = active.getBufferExpected(vertexResource);
                    auto indices  = active.getBufferExpected(indexResource);
                    if (!depth || !vertices || !indices)
                    {
                        return std::unexpected("Shadow graph resource is unavailable");
                    }
                    return recordShadowRaster(device,
                                              pipeline,
                                              cmd,
                                              uint32_t(cascade),
                                              **depth,
                                              **vertices,
                                              **indices,
                                              frameSet,
                                              materialSets,
                                              dynamicDraws ? *dynamicDraws : draws);
                });
            if (!added)
            {
                return added;
            }
        }
        return {};
    }
} // namespace vultra
