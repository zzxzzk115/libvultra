#include "image_decode.hpp"
#include "import_jobs.hpp"

#include <vultra/assets/texture_import.hpp>
#include <vultra/core/base/logger.hpp>

#include <bc7e_ispc.h>
#include <xxhash.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>

namespace vultra
{
    namespace
    {
        void compressBc7(TextureData& texture)
        {
            static std::once_flag initialized;
            std::call_once(initialized,
                           []
                           {
                               ispc::bc7e_compress_block_init();
                           });
            ispc::bc7e_compress_block_params parameters;
            ispc::bc7e_compress_block_params_init_fast(&parameters, false);
            // Bounded batches feed SIMD lanes; vtask already distributes independent textures.
            std::array<uint32_t, 64 * 16> pixels;
            std::array<uint64_t, 64 * 2>  encoded;
            for (auto& level : texture.levels)
            {
                const uint32_t         blocksX = (level.size.width + 3) / 4;
                const uint32_t         blocksY = (level.size.height + 3) / 4;
                const uint64_t         count   = uint64_t(blocksX) * blocksY;
                std::vector<std::byte> output(size_t(count) * 16);
                for (uint64_t first = 0; first < count; first += 64)
                {
                    const auto batch = uint32_t(std::min<uint64_t>(64, count - first));
                    for (uint32_t block = 0; block < batch; ++block)
                    {
                        const auto bx = uint32_t((first + block) % blocksX) * 4;
                        const auto by = uint32_t((first + block) / blocksX) * 4;
                        for (uint32_t y = 0; y < 4; ++y)
                        {
                            const auto sy = std::min(by + y, level.size.height - 1);
                            for (uint32_t x = 0; x < 4; ++x)
                            {
                                const auto sx = std::min(bx + x, level.size.width - 1);
                                std::memcpy(&pixels[block * 16 + y * 4 + x],
                                            level.bytes.data() + (size_t(sy) * level.size.width + sx) * 4,
                                            4);
                            }
                        }
                    }
                    ispc::bc7e_compress_blocks(batch, encoded.data(), pixels.data(), &parameters);
                    std::memcpy(output.data() + first * 16, encoded.data(), size_t(batch) * 16);
                }
                level.bytes = std::move(output);
            }
            texture.format = TextureFormat::eBc7Unorm;
        }
    } // namespace

    TextureData prepareTexture(const SceneImage& image, bool srgb, const TextureImportOptions& options)
    {
        if (!image.dds.empty())
        {
            auto result = asset_detail::decodeDds(image.dds, srgb, options.compression == TextureCompression::eNone);
            if (!options.mipmaps)
            {
                result.levels.resize(1);
            }
            return result;
        }
        if (image.width == 0 || image.height == 0 || image.width > uint32_t(std::numeric_limits<int>::max() / 4) ||
            image.height > uint32_t(std::numeric_limits<int>::max()) ||
            image.pixels.size() != uint64_t(image.width) * image.height * 4)
        {
            throw std::invalid_argument("SceneData image must contain RGBA8 pixels");
        }
        const auto                started = std::chrono::steady_clock::now();
        std::vector<TextureLevel> levels;
        TextureLevel              base {{image.width, image.height}, std::vector<std::byte>(image.pixels.size())};
        std::memcpy(base.bytes.data(), image.pixels.data(), image.pixels.size());
        levels.push_back(std::move(base));
        while (options.mipmaps && (levels.back().size.width > 1 || levels.back().size.height > 1))
        {
            const auto&  previous = levels.back();
            const Extent size {std::max(previous.size.width / 2, 1u), std::max(previous.size.height / 2, 1u)};
            TextureLevel next {size, std::vector<std::byte>(size_t(size.width) * size.height * 4)};
            // Keep alpha linear and filter channels independently, including packed material data.
            // stb_image_resize2 uses SSE2 on x64; its sRGB path replaces per-pixel pow calls.
            if (!stbir_resize(previous.bytes.data(),
                              int(previous.size.width),
                              int(previous.size.height),
                              0,
                              next.bytes.data(),
                              int(size.width),
                              int(size.height),
                              0,
                              STBIR_RGBA_NO_AW,
                              srgb ? STBIR_TYPE_UINT8_SRGB : STBIR_TYPE_UINT8,
                              STBIR_EDGE_CLAMP,
                              STBIR_FILTER_BOX))
            {
                throw std::runtime_error("Generate texture mip");
            }
            levels.push_back(std::move(next));
        }
        TextureData result {srgb ? TextureFormat::eRgba8Srgb : TextureFormat::eRgba8Unorm, std::move(levels)};
        const auto  mipsDone = std::chrono::steady_clock::now();
        if (!srgb && options.compression == TextureCompression::eBc7Linear && image.width >= 4 && image.height >= 4)
        {
            compressBc7(result);
        }
        Logger::core().debug(
            "Texture {}x{} {}: mips {:.1f} ms, compression {:.1f} ms",
            image.width,
            image.height,
            srgb ? "sRGB" : "linear",
            std::chrono::duration<double, std::milli>(mipsDone - started).count(),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mipsDone).count());
        return result;
    }

    PreparedTextures prepareTextures(const SceneData& scene, const TextureImportOptions& options, uint32_t workers)
    {
        PreparedTextures result;
        result.images.push_back(prepareTexture({1, 1, {255, 255, 255, 255}}, false, options));
        result.images.push_back(prepareTexture({1, 1, {128, 128, 255, 255}}, false, options));

        struct TextureJob
        {
            uint32_t image;
            uint32_t output;
            bool     srgb;
        };

        std::vector<TextureJob>                  jobs;
        std::map<std::pair<int, bool>, uint32_t> cache;
        std::vector<uint64_t>                    hashes(scene.images.size());
        asset_detail::runImportJobs("Indexing image content",
                                    uint32_t(scene.images.size()),
                                    workers,
                                    0,
                                    [&](uint32_t i)
                                    {
                                        const auto& image = scene.images[i];
                                        const auto  bytes = image.dds.empty() ? std::as_bytes(std::span(image.pixels)) :
                                                                                std::span<const std::byte>(image.dds);
                                        hashes[i]         = XXH3_64bits(bytes.data(), bytes.size());
                                    });
        std::multimap<uint64_t, uint32_t> content;
        for (const auto& material : scene.materials)
        {
            const int                                   images[] {material.baseColorTexture.image,
                                                                  material.metallicRoughnessTexture.image,
                                                                  material.normalTexture.image,
                                                                  material.occlusionTexture.image,
                                                                  material.emissionTexture.image,
                                                                  material.specularTexture.image,
                                                                  material.specularColorTexture.image};
            std::array<uint32_t, kMaterialTextureCount> slots {};
            for (uint32_t slot = 0; slot < slots.size(); ++slot)
            {
                slots[slot] = slot == 2 ? 1u : 0u;
                if (images[slot] < 0)
                {
                    continue;
                }
                const auto key   = std::make_pair(images[slot], slot == 0 || slot == 4 || slot == 6);
                auto       found = cache.find(key);
                if (found == cache.end())
                {
                    const auto& image       = scene.images.at(key.first);
                    auto        index       = uint32_t(jobs.size() + 2);
                    const auto [begin, end] = content.equal_range(hashes[key.first]);
                    for (auto candidate = begin; candidate != end; ++candidate)
                    {
                        const auto& job      = jobs[candidate->second];
                        const auto& previous = scene.images[job.image];
                        // Hashes only select candidates; compare bytes to rule out collisions.
                        if (job.srgb == key.second && previous.width == image.width &&
                            previous.height == image.height && previous.pixels == image.pixels &&
                            previous.dds == image.dds)
                        {
                            index = job.output;
                            break;
                        }
                    }
                    if (index == jobs.size() + 2)
                    {
                        content.emplace(hashes[key.first], uint32_t(jobs.size()));
                        jobs.push_back({uint32_t(key.first), index, key.second});
                    }
                    found = cache.emplace(key, index).first;
                }
                slots[slot] = found->second;
            }
            result.materials.push_back(slots);
        }
        if (jobs.empty())
        {
            return result;
        }
        Logger::core().info("Material textures: {} image/color-space references, {} unique payloads",
                            cache.size(),
                            jobs.size());
        // Fixed output indices keep materials and cache bytes independent of completion order.
        result.images.resize(jobs.size() + 2);
        std::ranges::stable_sort(jobs,
                                 [&scene](const TextureJob& a, const TextureJob& b)
                                 {
                                     return uint64_t(scene.images[a.image].width) * scene.images[a.image].height >
                                            uint64_t(scene.images[b.image].width) * scene.images[b.image].height;
                                 });
        const auto&        largestImage = scene.images[jobs.front().image];
        const uint64_t     largest      = uint64_t(largestImage.width) * largestImage.height * 4;
        constexpr uint64_t mib          = 1024 * 1024;
        // Account for the mip chain, block output and bounded SIMD batches. This is
        // a concurrency estimate, not an allocation guarantee; allocation errors still propagate.
        const uint64_t scratchPerJob = 16 * mib + largest * 2;
        Logger::core().info("Texture policy: mips {}, compression {}",
                            options.mipmaps ? "on" : "off",
                            options.compression == TextureCompression::eNone ? "none" : "BC7 linear");
        asset_detail::runImportJobs("Preparing textures",
                                    uint32_t(jobs.size()),
                                    workers,
                                    scratchPerJob,
                                    [&](uint32_t i)
                                    {
                                        const auto  job   = jobs[i];
                                        const auto& image = scene.images[job.image];
                                        Logger::core().debug("Preparing image {}: {}x{}, {}",
                                                             job.image,
                                                             image.width,
                                                             image.height,
                                                             job.srgb ? "sRGB" : "linear");
                                        auto& texture       = result.images[job.output];
                                        texture             = prepareTexture(image, job.srgb, options);
                                        texture.sourceImage = int(job.image);
                                        if (!image.dds.empty())
                                        {
                                            texture.sourceMipNum = uint32_t(texture.levels.size());
                                        }
                                        else if (texture.format != TextureFormat::eBc7Unorm)
                                        {
                                            texture.sourceMipNum = 1;
                                        }
                                    });
        return result;
    }
} // namespace vultra
