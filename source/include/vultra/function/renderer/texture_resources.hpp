#pragma once

#include "vultra/function/renderer/texture_manager.hpp"

namespace vultra
{
    namespace gfx
    {
        struct TextureInfo
        {
            rhi::TextureType  type {rhi::TextureType::eUndefined};
            Ref<rhi::Texture> texture;

            [[nodiscard]] auto isValid() const { return type != rhi::TextureType::eUndefined && texture && *texture; }

            friend bool operator==(const TextureInfo& a, const TextureInfo& b) { return a.texture == b.texture; }

            template<class Archive>
            void save(Archive& archive) const
            {
                archive(resource::serialize(texture));
            }
            template<class Archive>
            void load(Archive& archive)
            {
                std::optional<std::string> path;
                archive(path);
                if (path)
                {
                    texture = resource::loadResource<TextureManager>(*path);
                    if (texture)
                        type = texture->getType();
                }
            }
        };
        using TextureResources = std::map<std::string, TextureInfo, std::less<>>;

        [[nodiscard]] bool operator==(const TextureResources&, const TextureResources&);
    } // namespace gfx
} // namespace vultra