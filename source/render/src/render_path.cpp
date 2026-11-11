#include <vultra/render/render_path.hpp>

#include <algorithm>

namespace vultra
{
    std::expected<RenderGraph::Resource, Error> findSceneResource(std::span<const SceneResource> resources,
                                                                  std::string_view               name)
    {
        const auto found = std::ranges::find(resources, name, &SceneResource::name);
        if (found == resources.end())
        {
            return makeError("Render path does not provide {}", name);
        }
        return found->resource;
    }

    std::expected<void, Error>
    validateSceneResources(const RenderPath& path, std::span<const SceneResource> resources, const RenderGraph& graph)
    {
        const auto provided = path.provides();
        for (const auto required : {kSceneHdr, kSceneDepth})
        {
            if (std::ranges::find(provided, required, &SceneResourceDesc::name) == provided.end())
            {
                return makeError("Render path must provide {}", required);
            }
        }
        if (resources.size() != provided.size())
        {
            return makeError("Render path returned a different resource set than it provides");
        }
        for (const auto& desc : provided)
        {
            const auto resource = findSceneResource(resources, desc.name);
            if (!resource)
            {
                return std::unexpected(resource.error());
            }
            const auto info = graph.resourceInfo(*resource);
            if (!info)
            {
                return std::unexpected(info.error());
            }
            if (!info->isTexture || info->textureDesc.format != desc.format)
            {
                return makeError("Render path resource does not match its declared format: {}", desc.name);
            }
        }
        return {};
    }
} // namespace vultra
