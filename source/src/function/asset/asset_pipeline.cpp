#include "cache_io.hpp"
#include "import_jobs.hpp"
#include "texture_layout.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/core/os/file.hpp>
#include <vultra/function/asset/asset_pipeline.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <format>
#include <fstream>
#include <map>

namespace vultra
{
    namespace
    {
        using asset_detail::CacheReader;
        using asset_detail::CacheWriter;
        using Json = nlohmann::json;

        constexpr uint64_t kMagic = 0x0054455353414c56; // "VLASSET\0", little endian.
        // Bump for changed loader output, mip filtering, compression, serialization or dependency rules.
        constexpr uint32_t kPipelineVersion = 8;
        static_assert(std::endian::native == std::endian::little);

        std::string pathText(const std::filesystem::path& path)
        {
            const auto text = path.generic_u8string();
            return {text.begin(), text.end()};
        }

        std::string hashBytes(std::span<const std::byte> bytes)
        {
            return std::format("{:016x}", XXH3_64bits(bytes.data(), bytes.size()));
        }

        Json importRecipe(const std::filesystem::path& source, const AssetImportOptions& options)
        {
            return {{"version", kPipelineVersion},
                    {"source", pathText(source)},
                    {"mipmaps", options.textures.mipmaps},
                    {"compression", int(options.textures.compression)},
                    {"bc7_encoder", "bc7e-ispc-fast-v1"}};
        }

        std::filesystem::path
        cacheFilePath(const std::filesystem::path& source, const Json& recipe, const AssetImportOptions& options)
        {
            const auto text = recipe.dump();
            const auto key  = hashBytes(std::as_bytes(std::span(text)));
            return options.cacheDirectory /
                   (source.stem().wstring() + L"-" + std::filesystem::path(key).wstring() + L".vasset");
        }

        std::string hashFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Missing asset dependency: " + pathText(path));
            }
            std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state(XXH3_createState(), &XXH3_freeState);
            if (!state || XXH3_64bits_reset(state.get()) == XXH_ERROR)
            {
                throw std::runtime_error("Initialize asset hash");
            }
            std::array<char, 65536> buffer;
            while (file)
            {
                file.read(buffer.data(), std::streamsize(buffer.size()));
                XXH3_64bits_update(state.get(), buffer.data(), size_t(file.gcount()));
            }
            if (!file.eof())
            {
                throw std::runtime_error("Read asset dependency: " + pathText(path));
            }
            return std::format("{:016x}", XXH3_64bits_digest(state.get()));
        }

        std::vector<std::byte> readFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            file.exceptions(std::ios::failbit | std::ios::badbit);
            const auto size = file.tellg();
            if (size < 0)
            {
                throw std::runtime_error("Read asset cache size");
            }
            std::vector<std::byte> bytes(static_cast<size_t>(size));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
            return bytes;
        }

        void validateDependencies(const Json& dependencies, uint32_t workers)
        {
            if (dependencies.empty())
            {
                throw std::runtime_error("Asset cache has no source dependencies");
            }
            asset_detail::runImportJobs("Verifying source dependencies",
                                        uint32_t(dependencies.size()),
                                        workers,
                                        65536,
                                        [&](uint32_t i)
                                        {
                                            const auto& dependency = dependencies.at(i);
                                            const auto  name       = dependency.at("path").get<std::string>();
                                            if (hashFile(std::filesystem::u8path(name)) !=
                                                dependency.at("hash").get<std::string>())
                                            {
                                                throw std::runtime_error("Changed asset dependency: " + name);
                                            }
                                        });
        }

        Json readMetadata(CacheReader& archive, std::span<const std::byte> bytes, const Json& recipe)
        {
            if (archive.value<uint64_t>() != kMagic || archive.value<uint32_t>() != kPipelineVersion)
            {
                throw std::runtime_error("Asset cache format changed");
            }
            const auto       checksum = archive.value<uint64_t>();
            constexpr size_t prefix   = sizeof(uint64_t) * 2 + sizeof(uint32_t);
            if (XXH3_64bits(bytes.data() + prefix, bytes.size() - prefix) != checksum)
            {
                throw std::runtime_error("Asset cache checksum mismatch");
            }
            const auto text     = archive.array<char>();
            const auto metadata = Json::parse(text);
            if (metadata.at("recipe") != recipe)
            {
                throw std::runtime_error("Asset import recipe changed");
            }
            return metadata;
        }

        void writeTextures(CacheWriter& writer, const PreparedTextures& textures)
        {
            // Only generated mips and newly encoded BC7 blocks belong in the cache.
            size_t bytes = sizeof(uint64_t) + std::as_bytes(std::span(textures.materials)).size() + sizeof(uint32_t);
            for (const auto& texture : textures.images)
            {
                bytes += sizeof(texture.format) + sizeof(texture.sourceImage) + 2 * sizeof(uint32_t);
                for (uint32_t mip = 0; mip < texture.levels.size(); ++mip)
                {
                    bytes += sizeof(Extent) + sizeof(uint64_t);
                    if (mip >= texture.sourceMipNum)
                    {
                        bytes += texture.levels[mip].bytes.size();
                    }
                }
            }
            writer.data.reserve(writer.data.size() + bytes);
            writer.array(textures.materials);
            writer.value(uint32_t(textures.images.size()));
            for (const auto& texture : textures.images)
            {
                writer.value(texture.format);
                writer.value(texture.sourceImage);
                writer.value(texture.sourceMipNum);
                writer.value(uint32_t(texture.levels.size()));
                for (uint32_t mip = 0; mip < texture.levels.size(); ++mip)
                {
                    const auto& level = texture.levels[mip];
                    writer.value(level.size);
                    writer.array(mip < texture.sourceMipNum ? std::span<const std::byte>() :
                                                              std::span<const std::byte>(level.bytes));
                }
            }
        }

        PreparedTextures readTextures(CacheReader& reader, const Scene& scene, const AssetImportOptions& options)
        {
            PreparedTextures result;
            result.materials = reader.array<std::array<uint32_t, kMaterialTextureCount>>();
            const auto count = reader.value<uint32_t>();
            if (count > 1000000 || result.materials.size() != scene.materials.size())
            {
                throw std::runtime_error("Invalid cached texture/material count");
            }
            result.images.resize(count);
            std::vector<std::vector<std::span<const std::byte>>> payloads(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                auto& texture        = result.images[i];
                texture.format       = reader.value<VriFormat>();
                texture.sourceImage  = reader.value<int>();
                texture.sourceMipNum = reader.value<uint32_t>();
                const auto levels    = reader.value<uint32_t>();
                const auto layout    = asset_detail::textureLayout(texture.format);
                if (levels == 0 || levels > 32 || texture.sourceMipNum > levels || texture.sourceImage < -1 ||
                    (texture.sourceImage >= 0 && size_t(texture.sourceImage) >= scene.images.size()) ||
                    (texture.sourceMipNum != 0 && texture.sourceImage < 0))
                {
                    throw std::runtime_error("Invalid cached texture source or mip count");
                }
                texture.levels.resize(levels);
                for (uint32_t mip = 0; mip < levels; ++mip)
                {
                    auto& level             = texture.levels[mip];
                    level.size              = reader.value<Extent>();
                    const auto     pixels   = reader.arrayBytes<std::byte>();
                    const uint64_t expected = mip < texture.sourceMipNum ?
                                                  0 :
                                                  uint64_t((level.size.width + layout.block - 1) / layout.block) *
                                                      ((level.size.height + layout.block - 1) / layout.block) *
                                                      layout.bytes;
                    if (level.size.empty() || pixels.size() != expected)
                    {
                        throw std::runtime_error("Invalid cached derived mip size");
                    }
                    if (mip > 0)
                    {
                        const auto previous = texture.levels[mip - 1].size;
                        if (level.size != Extent {std::max(1u, previous.width / 2), std::max(1u, previous.height / 2)})
                        {
                            throw std::runtime_error("Invalid cached mip dimensions");
                        }
                    }
                    payloads[i].push_back(pixels);
                }
            }
            if (!reader.empty())
            {
                throw std::runtime_error("Trailing derived cache data");
            }
            for (const auto& material : result.materials)
            {
                for (auto image : material)
                {
                    if (image >= count)
                    {
                        throw std::runtime_error("Invalid cached material texture");
                    }
                }
            }
            asset_detail::runImportJobs(
                "Restoring derived textures",
                count,
                options.workers,
                64 * 1024 * 1024,
                [&](uint32_t i)
                {
                    auto& texture = result.images[i];
                    if (texture.sourceMipNum != 0)
                    {
                        const auto& image = scene.images[texture.sourceImage];
                        const bool  srgb =
                            texture.format == VriFormat_RGBA8_SRGB || texture.format == VriFormat_BGRA8_SRGB;
                        TextureImportOptions sourceOptions;
                        sourceOptions.mipmaps = texture.sourceMipNum > 1;
                        sourceOptions.compression =
                            image.dds.empty() ? TextureCompression::eNone : options.textures.compression;
                        auto authored = prepareTexture(image, srgb, sourceOptions);
                        if (authored.format != texture.format || authored.levels.size() != texture.sourceMipNum)
                        {
                            throw std::runtime_error("Cached texture no longer matches its source");
                        }
                        for (uint32_t mip = 0; mip < texture.sourceMipNum; ++mip)
                        {
                            if (authored.levels[mip].size != texture.levels[mip].size)
                            {
                                throw std::runtime_error("Cached dimensions no longer match the source");
                            }
                            texture.levels[mip].bytes = std::move(authored.levels[mip].bytes);
                        }
                    }
                    for (uint32_t mip = texture.sourceMipNum; mip < texture.levels.size(); ++mip)
                    {
                        const auto bytes = payloads[i][mip];
                        texture.levels[mip].bytes.assign(bytes.begin(), bytes.end());
                    }
                });
            return result;
        }
    } // namespace

    bool isAssetCacheCurrent(const std::filesystem::path& input, const AssetImportOptions& options)
    {
        if (!options.cache)
        {
            return false;
        }
        const auto source    = std::filesystem::canonical(input);
        const auto recipe    = importRecipe(source, options);
        const auto cachePath = cacheFilePath(source, recipe, options);
        if (!std::filesystem::is_regular_file(cachePath))
        {
            return false;
        }
        try
        {
            const auto  bytes = readFile(cachePath);
            CacheReader archive(bytes);
            const auto  metadata = readMetadata(archive, bytes, recipe);
            validateDependencies(metadata.at("dependencies"), options.workers);
            return true;
        }
        catch (const std::exception& error)
        {
            Logger::core().info("Asset cache needs import: {}: {}", pathText(input), error.what());
            return false;
        }
    }

    ImportedAsset importAsset(const std::filesystem::path& input, const AssetImportOptions& options)
    {
        const auto started   = std::chrono::steady_clock::now();
        const auto source    = std::filesystem::canonical(input);
        const auto recipe    = importRecipe(source, options);
        const auto cachePath = cacheFilePath(source, recipe, options);
        Logger::core().info("Importing asset: {}", pathText(input));
        Logger::core().info("Loading geometry, materials and source images...");
        const auto                         loadStarted = std::chrono::steady_clock::now();
        auto                               lastReadLog = loadStarted;
        std::map<std::string, std::string> files;
        const SourceObserver observer = [&files, &lastReadLog](const auto& path, std::span<const std::byte> bytes)
        {
            const auto name              = pathText(std::filesystem::canonical(path));
            const auto hash              = hashBytes(bytes);
            const auto [entry, inserted] = files.emplace(name, hash);
            const auto now               = std::chrono::steady_clock::now();
            if (now - lastReadLog >= std::chrono::milliseconds(500))
            {
                Logger::core().info("Read {} source files; {}", files.size(), pathText(path.filename()));
                lastReadLog = now;
            }
            if (!inserted && entry->second != hash)
            {
                throw std::runtime_error("Asset changed while importing: " + name);
            }
        };
        ImportedAsset asset;
        auto          extension = source.extension().string();
        std::ranges::transform(extension,
                               extension.begin(),
                               [](unsigned char value)
                               {
                                   return char(std::tolower(value));
                               });
        if (extension == ".gltf" || extension == ".glb")
        {
            asset.scene = loadGltf(source, observer, options.workers);
        }
        else if (extension == ".obj")
        {
            asset.scene = loadObj(source, observer, options.workers);
        }
        else if (extension == ".fbx")
        {
            asset.scene = loadFbx(source, observer, options.workers);
        }
        else
        {
            throw std::invalid_argument("Unsupported asset extension: " + extension);
        }
        Logger::core().info(
            "Loaded {} triangles, {} materials, {} source images ({:.1f} ms)",
            asset.scene.indices.size() / 3,
            asset.scene.materials.size(),
            asset.scene.images.size(),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadStarted).count());
        Json dependencies = Json::array();
        for (const auto& [path, hash] : files)
        {
            dependencies.push_back({{"path", path}, {"hash", hash}});
        }
        if (options.cache && !options.reimport && std::filesystem::exists(cachePath))
        {
            try
            {
                Logger::core().info("Checking derived cache against loaded source content: {}", pathText(input));
                const auto  bytes = readFile(cachePath);
                CacheReader archive(bytes);
                const auto  metadata = readMetadata(archive, bytes, recipe);
                if (metadata.at("dependencies") != dependencies)
                {
                    throw std::runtime_error("Changed asset source dependencies");
                }
                asset.textures = readTextures(archive, asset.scene, options);
                asset.scene.images.clear();
                asset.cacheHit  = true;
                asset.cachePath = cachePath;
                Logger::core().info(
                    "Asset cache hit: {} ({:.1f} ms)",
                    pathText(input),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
                return asset;
            }
            catch (const std::exception& error)
            {
                Logger::core().info("Reimport {}: {}", pathText(input), error.what());
            }
        }
        const auto textureStarted = std::chrono::steady_clock::now();
        asset.textures            = prepareTextures(asset.scene, options.textures, options.workers);
        Logger::core().info(
            "Texture preparation complete ({:.1f} ms)",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - textureStarted).count());
        asset.scene.images.clear();
        // Do not publish a mixture of source revisions if an editor changed a dependency during import.
        Logger::core().info("Verifying {} source dependencies before publishing...", files.size());
        validateDependencies(dependencies, options.workers);
        asset.cachePath = cachePath;
        if (options.cache)
        {
            try
            {
                Logger::core().info("Writing asset cache: {}", pathText(cachePath));
                const auto  writeStarted = std::chrono::steady_clock::now();
                const auto  metadata     = Json {{"recipe", recipe}, {"dependencies", dependencies}}.dump();
                CacheWriter archive;
                archive.value(kMagic);
                archive.value(kPipelineVersion);
                const auto checksumOffset = archive.data.size();
                archive.value(uint64_t(0));
                const auto payloadOffset = archive.data.size();
                archive.array(std::span<const char>(metadata));
                writeTextures(archive, asset.textures);
                const auto checksum =
                    XXH3_64bits(archive.data.data() + payloadOffset, archive.data.size() - payloadOffset);
                std::memcpy(archive.data.data() + checksumOffset, &checksum, sizeof(checksum));
                std::filesystem::create_directories(options.cacheDirectory);
                writeFileAtomically(cachePath, archive.data);
                Logger::core().info(
                    "Asset cache written: {:.1f} MiB ({:.1f} ms)",
                    double(archive.data.size()) / (1024 * 1024),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - writeStarted).count());
            }
            catch (const std::exception& error)
            {
                Logger::core().warn("Asset imported, but cache could not be written: {}", error.what());
            }
        }
        Logger::core().info(
            "Imported {}: {} triangles, {} textures ({:.1f} ms)",
            pathText(input),
            asset.scene.indices.size() / 3,
            asset.textures.images.size(),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        return asset;
    }
} // namespace vultra
