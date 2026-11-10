#include <vultra/servers/rendering/builtin/frame_data.hpp>
#include <vultra/servers/rendering/builtin/gbuffer_graph.hpp>

#include <set>

namespace vultra
{
    std::expected<std::array<RenderGraph::Resource, 7>, std::string>
    addGBufferGraphPasses(RenderGraph&           graph,
                          const GBufferPipeline& pipeline,
                          std::string            name,
                          GBufferGraphInputs     inputs)
    {
        auto& device     = graph.device();
        auto  frame      = graph.getBufferExpected(inputs.frame);
        auto  vertices   = graph.getBufferExpected(inputs.vertices);
        auto  indices    = graph.getBufferExpected(inputs.indices);
        auto  transforms = graph.getBufferExpected(inputs.transforms);
        auto  depth      = graph.getTextureExpected(inputs.depth);
        if (!pipeline.belongsTo(device) || !inputs.frameSet || !frame || !vertices || !indices || !transforms ||
            !depth || !((*frame)->desc.usage & VriBufferUsage_ConstantBuffer) ||
            (*frame)->desc.size < sizeof(BuiltinFrameData) ||
            !((*vertices)->desc.usage & VriBufferUsage_VertexBuffer) ||
            !((*vertices)->desc.usage & VriBufferUsage_StorageBuffer) ||
            !((*indices)->desc.usage & VriBufferUsage_IndexBuffer) ||
            !((*indices)->desc.usage & VriBufferUsage_StorageBuffer) ||
            !((*transforms)->desc.usage & VriBufferUsage_StorageBuffer) ||
            (*depth)->desc.format != VriFormat_D32_SFLOAT || (*depth)->desc.type != VriTextureType_2D ||
            !(*depth)->desc.width || !(*depth)->desc.height || (*depth)->desc.layerNum != 1 ||
            (*depth)->desc.sampleNum != 1 || !((*depth)->desc.usage & VriTextureUsage_DepthStencilAttachment))
            return std::unexpected("Invalid G-buffer graph inputs");
        if (inputs.materialTextures.empty() && (inputs.dynamicDraws || !inputs.draws.empty()))
            return std::unexpected("G-buffer requires its sampled material textures");
        std::set<const Texture*> unique;
        for (const auto resource : inputs.materialTextures)
        {
            auto texture = graph.getTextureExpected(resource);
            if (!texture || !((*texture)->desc.usage & VriTextureUsage_ShaderResource) ||
                !unique.insert(*texture).second)
                return std::unexpected("G-buffer material textures must be distinct sampled resources");
        }
        const Extent                         size {(*depth)->desc.width, (*depth)->desc.height};
        const auto                           formats = gbufferFormats(pipeline.positionEncoding());
        std::array<RenderGraph::Resource, 7> outputs {};
        for (size_t i = 0; i < outputs.size(); ++i)
        {
            auto desc = colorTexture(size, formats[i]);
            if (i == 0)
                desc.clearValue.color.f32[pipeline.positionEncoding() == GBufferPositionEncoding::eDepth32 ? 1 : 3] =
                    -1;
            auto texture = graph.createTextureExpected(name + "." + std::to_string(i), desc);
            if (!texture)
                return std::unexpected(texture.error());
            outputs[i] = *texture;
        }
        for (uint32_t stage = pipeline.singlePass() ? 2 : 0; stage < (pipeline.singlePass() ? 3u : 2u); ++stage)
        {
            const uint32_t                first = stage == 1 ? 4 : 0;
            const uint32_t                count = stage == 0 ? 4 : stage == 1 ? 3 : 7;
            std::vector<RenderGraph::Use> uses {
                {inputs.frame, Usage::eConstantRead},
                {inputs.vertices, Usage::eStorageRead},
                {inputs.indices, Usage::eStorageRead},
                {inputs.transforms, Usage::eStorageRead},
                {inputs.depth, stage != 1 ? Usage::eDepthWrite : Usage::eDepthRead, stage != 1}};
            for (const auto texture : inputs.materialTextures)
                uses.push_back({texture, Usage::eSampled});
            for (uint32_t i = first; i < first + count; ++i)
                uses.push_back({outputs[i], Usage::eColorWrite, true});
            auto added = graph.addPassExpected(name + (stage == 0 ? ".geometry" :
                                                       stage == 1 ? ".material" :
                                                                    ".combined"),
                                               uses,
                                               [&device, &pipeline, inputs, outputs, stage, first, count](
                                                   VriCommandBuffer* commands,
                                                   RenderGraph&      active) -> std::expected<void, std::string>
                                               {
                                                   std::array<Texture*, 7> targets {};
                                                   for (uint32_t i = first; i < first + count; ++i)
                                                   {
                                                       auto target = active.getTextureExpected(outputs[i]);
                                                       if (!target)
                                                           return std::unexpected(target.error());
                                                       targets[i] = *target;
                                                   }
                                                   auto depth    = active.getTextureExpected(inputs.depth);
                                                   auto vertices = active.getBufferExpected(inputs.vertices);
                                                   auto indices  = active.getBufferExpected(inputs.indices);
                                                   if (!depth || !vertices || !indices)
                                                       return std::unexpected("G-buffer graph resource unavailable");
                                                   const auto& draws =
                                                       inputs.dynamicDraws ? *inputs.dynamicDraws : inputs.draws;
                                                   return recordGBufferGeometry(device,
                                                                                pipeline,
                                                                                commands,
                                                                                stage,
                                                                                targets,
                                                                                **depth,
                                                                                **vertices,
                                                                                **indices,
                                                                                inputs.frameSet,
                                                                                inputs.materialSets,
                                                                                draws);
                                               });
            if (!added)
                return std::unexpected(added.error());
        }
        return outputs;
    }
} // namespace vultra
