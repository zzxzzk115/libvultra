#include <vultra/render/render_path.hpp>

#include <algorithm>
#include <stdexcept>

namespace vultra
{
    RenderGraph::Resource findSceneResource(std::span<const SceneResource> resources, std::string_view name)
    {
        const auto found = std::ranges::find(resources, name, &SceneResource::name);
        if (found == resources.end())
        {
            throw std::invalid_argument("Render path does not provide " + std::string(name));
        }
        return found->resource;
    }

    void validateSceneResources(const RenderPath&              path,
                                std::span<const SceneResource> resources,
                                const RenderGraph&             graph)
    {
        const auto provided = path.provides();
        for (const auto required : {kSceneHdr, kSceneDepth})
        {
            if (std::ranges::find(provided, required, &SceneResourceDesc::name) == provided.end())
            {
                throw std::logic_error("Render path must provide " + std::string(required));
            }
        }
        if (resources.size() != provided.size())
        {
            throw std::logic_error("Render path returned a different resource set than it provides");
        }
        for (const auto& desc : provided)
        {
            const auto info = graph.resourceInfo(findSceneResource(resources, desc.name));
            if (!info.isTexture || info.textureDesc.format != desc.format)
            {
                throw std::logic_error("Render path resource does not match its declared format: " + desc.name);
            }
        }
    }
} // namespace vultra
