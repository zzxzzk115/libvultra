#pragma once

#include <vultra/assets/scene_data.hpp>

namespace fixture
{
    inline vultra::SceneData texturedQuad()
    {
        vultra::SceneData scene;
        scene.vertices   = {{{-0.7f, -0.6f, 0.5f}, {0, 0, 1}, {0, 1}},
                            {{0.7f, -0.6f, 0.5f}, {0, 0, 1}, {1, 1}},
                            {{0.7f, 0.6f, 0.5f}, {0, 0, 1}, {1, 0}},
                            {{-0.7f, 0.6f, 0.5f}, {0, 0, 1}, {0, 0}}};
        scene.indices    = {0, 1, 2, 0, 2, 3};
        scene.primitives = {{0, 6, 0}};
        scene.materials.resize(1);
        scene.materials[0].baseColorTexture = {0, 0};
        vultra::SceneSampler sampler;
        sampler.minFilter  = vultra::SamplerFilter::eNearest;
        sampler.magFilter  = vultra::SamplerFilter::eNearest;
        sampler.mipmapMode = vultra::SamplerMipmapMode::eNearest;
        scene.samplers.push_back(sampler);
        vultra::SceneImage image;
        image.width  = 13;
        image.height = 7;
        image.pixels.resize(image.width * image.height * 4);
        for (uint32_t y = 0; y < image.height; ++y)
        {
            for (uint32_t x = 0; x < image.width; ++x)
            {
                const auto offset        = (y * image.width + x) * 4;
                image.pixels[offset]     = uint8_t(40 + ((x / 3) % 2) * 160);
                image.pixels[offset + 1] = uint8_t(30 + ((y / 2) % 2) * 180);
                image.pixels[offset + 2] = uint8_t(50 + ((x + y) % 5) * 35);
                image.pixels[offset + 3] = 255;
            }
        }
        scene.images.push_back(std::move(image));
        return scene;
    }
} // namespace fixture
