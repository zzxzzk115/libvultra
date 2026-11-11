#include "cache_io.hpp"
#include "import_jobs.hpp"
#include "texture_layout.hpp"

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/assets/asset_source.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <algorithm>
#include <bit>
#include <cctype>
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
        // Pre-release cache format stays at 1; format changes invalidate old local caches.
        constexpr uint32_t kPipelineVersion = 1;
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

        std::expected<std::filesystem::path, Error> canonicalPath(const std::filesystem::path& path)
        {
            std::error_code error;
            auto            result = std::filesystem::canonical(path, error);
            if (error)
            {
                return makeError("Resolve asset {}: {}", pathText(path), error.message());
            }
            return result;
        }

        // The name a source file has inside a cache. Through an asset source it is relative to the source's
        // root, so the cache stays current when that root moves, as it does for a packed project.
        std::expected<std::string, Error> cacheName(const std::filesystem::path& path, const AssetSource* assetSource)
        {
            const auto resolved = assetSource ? assetSource->resolve(path) : canonicalPath(path);
            if (!resolved)
            {
                return std::unexpected(resolved.error());
            }
            return pathText(assetSource ? resolved->lexically_relative(assetSource->root()) : *resolved);
        }

        // dump() is the only throwing nlohmann call here: it rejects names that are not valid UTF-8.
        std::expected<std::string, Error> dumpJson(const Json& value)
        {
            try
            {
                return value.dump();
            }
            catch (const Json::type_error& error)
            {
                return makeError("Serialize asset cache metadata: {}", error.what());
            }
        }

        Json
        importRecipe(const std::filesystem::path& source, const std::string& name, const AssetImportOptions& options)
        {
            Json recipe {{"version", kPipelineVersion},
                         {"source", name},
                         {"mipmaps", options.textures.mipmaps},
                         {"compression", int(options.textures.compression)},
                         {"bc7_encoder", "bc7e-ispc-fast-v1"}};
            auto extension = source.extension().string();
            std::ranges::transform(extension,
                                   extension.begin(),
                                   [](unsigned char c)
                                   {
                                       return char(std::tolower(c));
                                   });
            if (extension == ".fbx")
            {
                recipe["fbx"] = {{"importer", "static-orca-opaque-v1"},
                                 {"materials", int(options.fbx.materialConvention)},
                                 {"directx_normals", options.fbx.directXNormalMaps}};
            }
            return recipe;
        }

        std::expected<std::filesystem::path, Error>
        cacheFilePath(const std::filesystem::path& source, const Json& recipe, const AssetImportOptions& options)
        {
            const auto text = dumpJson(recipe);
            if (!text)
            {
                return std::unexpected(text.error());
            }
            const auto key = hashBytes(std::as_bytes(std::span(*text)));
            return options.cacheDirectory /
                   (source.stem().wstring() + L"-" + std::filesystem::path(key).wstring() + L".vasset");
        }

        // The resolved source, its import recipe and its cache file.
        struct CacheTarget
        {
            std::filesystem::path source;
            Json                  recipe;
            std::filesystem::path cachePath;
        };

        std::expected<CacheTarget, Error> cacheTarget(const std::filesystem::path& input,
                                                      const AssetImportOptions&    options,
                                                      const AssetSource*           assetSource)
        {
            auto source = assetSource ? assetSource->resolve(input) : canonicalPath(input);
            if (!source)
            {
                return std::unexpected(source.error());
            }
            const auto name = cacheName(input, assetSource);
            if (!name)
            {
                return std::unexpected(name.error());
            }
            auto recipe    = importRecipe(*source, *name, options);
            auto cachePath = cacheFilePath(*source, recipe, options);
            if (!cachePath)
            {
                return std::unexpected(cachePath.error());
            }
            return CacheTarget {std::move(*source), std::move(recipe), std::move(*cachePath)};
        }

        std::expected<std::string, Error> hashFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                return makeError("Missing asset dependency: {}", pathText(path));
            }
            std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state(XXH3_createState(), &XXH3_freeState);
            if (!state || XXH3_64bits_reset(state.get()) == XXH_ERROR)
            {
                return makeError("Initialize asset hash");
            }
            std::array<char, 65536> buffer;
            while (file)
            {
                file.read(buffer.data(), std::streamsize(buffer.size()));
                XXH3_64bits_update(state.get(), buffer.data(), size_t(file.gcount()));
            }
            if (!file.eof())
            {
                return makeError("Read asset dependency: {}", pathText(path));
            }
            return std::format("{:016x}", XXH3_64bits_digest(state.get()));
        }

        std::expected<std::vector<std::byte>, Error> readFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            const auto    size = file.tellg();
            if (!file || size < 0)
            {
                return makeError("Read asset cache size");
            }
            std::vector<std::byte> bytes(static_cast<size_t>(size));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
            {
                return makeError("Read asset cache: {}", pathText(path));
            }
            return bytes;
        }

        std::expected<void, Error>
        validateDependencies(const Json& dependencies, uint32_t workers, const AssetSource* source)
        {
            if (!dependencies.is_array() || dependencies.empty())
            {
                return makeError("Asset cache has no source dependencies");
            }
            return asset_detail::runImportJobs("Verifying source dependencies",
                                               uint32_t(dependencies.size()),
                                               workers,
                                               65536,
                                               [&](uint32_t i) -> std::expected<void, Error>
                                               {
                                                   const auto& dependency = dependencies[i];
                                                   const auto  name       = dependency.find("path");
                                                   const auto  recorded   = dependency.find("hash");
                                                   if (name == dependency.end() || !name->is_string() ||
                                                       recorded == dependency.end() || !recorded->is_string())
                                                   {
                                                       return makeError("Invalid asset cache dependency");
                                                   }
                                                   const auto& text = name->get_ref<const std::string&>();
                                                   const std::filesystem::path dependencyPath =
                                                       std::u8string(text.begin(), text.end());
                                                   std::expected<std::string, Error> hash;
                                                   if (source)
                                                   {
                                                       const auto bytes = source->read(dependencyPath);
                                                       if (!bytes)
                                                       {
                                                           return std::unexpected(bytes.error());
                                                       }
                                                       hash = hashBytes(*bytes);
                                                   }
                                                   else
                                                   {
                                                       hash = hashFile(dependencyPath);
                                                   }
                                                   if (!hash)
                                                   {
                                                       return std::unexpected(hash.error());
                                                   }
                                                   if (*hash != recorded->get_ref<const std::string&>())
                                                   {
                                                       return makeError("Changed asset dependency: {}", text);
                                                   }
                                                   return {};
                                               });
        }

        std::expected<Json, Error>
        readMetadata(CacheReader& archive, std::span<const std::byte> bytes, const Json& recipe)
        {
            const auto magic = archive.value<uint64_t>();
            if (!magic)
            {
                return std::unexpected(magic.error());
            }
            const auto version = archive.value<uint32_t>();
            if (!version)
            {
                return std::unexpected(version.error());
            }
            if (*magic != kMagic || *version != kPipelineVersion)
            {
                return makeError("Asset cache format changed");
            }
            const auto checksum = archive.value<uint64_t>();
            if (!checksum)
            {
                return std::unexpected(checksum.error());
            }
            constexpr size_t prefix = sizeof(uint64_t) * 2 + sizeof(uint32_t);
            if (XXH3_64bits(bytes.data() + prefix, bytes.size() - prefix) != *checksum)
            {
                return makeError("Asset cache checksum mismatch");
            }
            const auto text = archive.array<char>();
            if (!text)
            {
                return std::unexpected(text.error());
            }
            auto metadata = Json::parse(*text, nullptr, false);
            if (metadata.is_discarded() || !metadata.is_object())
            {
                return makeError("Invalid asset cache metadata");
            }
            const auto cachedRecipe = metadata.find("recipe");
            if (cachedRecipe == metadata.end() || *cachedRecipe != recipe)
            {
                return makeError("Asset import recipe changed");
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

        std::expected<PreparedTextures, Error>
        readTextures(CacheReader& reader, const SceneData& scene, const AssetImportOptions& options)
        {
            PreparedTextures result;
            auto             materials = reader.array<std::array<uint32_t, kMaterialTextureCount>>();
            if (!materials)
            {
                return std::unexpected(materials.error());
            }
            result.materials = std::move(*materials);
            const auto count = reader.value<uint32_t>();
            if (!count)
            {
                return std::unexpected(count.error());
            }
            if (*count > 1000000 || result.materials.size() != scene.materials.size())
            {
                return makeError("Invalid cached texture/material count");
            }
            result.images.resize(*count);
            std::vector<std::vector<std::span<const std::byte>>> payloads(*count);
            for (uint32_t i = 0; i < *count; ++i)
            {
                auto&      texture = result.images[i];
                const auto format  = reader.value<TextureFormat>();
                if (!format)
                {
                    return std::unexpected(format.error());
                }
                const auto sourceImage = reader.value<int>();
                if (!sourceImage)
                {
                    return std::unexpected(sourceImage.error());
                }
                const auto sourceMipNum = reader.value<uint32_t>();
                if (!sourceMipNum)
                {
                    return std::unexpected(sourceMipNum.error());
                }
                const auto levelCount = reader.value<uint32_t>();
                if (!levelCount)
                {
                    return std::unexpected(levelCount.error());
                }
                texture.format       = *format;
                texture.sourceImage  = *sourceImage;
                texture.sourceMipNum = *sourceMipNum;
                const auto levels    = *levelCount;
                const auto layout    = asset_detail::textureLayout(texture.format);
                if (!layout)
                {
                    return std::unexpected(layout.error());
                }
                if (levels == 0 || levels > 32 || texture.sourceMipNum > levels || texture.sourceImage < -1 ||
                    (texture.sourceImage >= 0 && size_t(texture.sourceImage) >= scene.images.size()) ||
                    (texture.sourceMipNum != 0 && texture.sourceImage < 0))
                {
                    return makeError("Invalid cached texture source or mip count");
                }
                texture.levels.resize(levels);
                for (uint32_t mip = 0; mip < levels; ++mip)
                {
                    auto&      level = texture.levels[mip];
                    const auto size  = reader.value<Extent>();
                    if (!size)
                    {
                        return std::unexpected(size.error());
                    }
                    level.size        = *size;
                    const auto pixels = reader.arrayBytes<std::byte>();
                    if (!pixels)
                    {
                        return std::unexpected(pixels.error());
                    }
                    const uint64_t expected = mip < texture.sourceMipNum ?
                                                  0 :
                                                  uint64_t((level.size.width + layout->block - 1) / layout->block) *
                                                      ((level.size.height + layout->block - 1) / layout->block) *
                                                      layout->bytes;
                    if (level.size.empty() || pixels->size() != expected)
                    {
                        return makeError("Invalid cached derived mip size");
                    }
                    if (mip > 0)
                    {
                        const auto previous = texture.levels[mip - 1].size;
                        if (level.size != Extent {std::max(1u, previous.width / 2), std::max(1u, previous.height / 2)})
                        {
                            return makeError("Invalid cached mip dimensions");
                        }
                    }
                    payloads[i].push_back(*pixels);
                }
            }
            if (!reader.empty())
            {
                return makeError("Trailing derived cache data");
            }
            for (const auto& material : result.materials)
            {
                for (auto image : material)
                {
                    if (image >= *count)
                    {
                        return makeError("Invalid cached material texture");
                    }
                }
            }
            const auto restored = asset_detail::runImportJobs(
                "Restoring derived textures",
                *count,
                options.workers,
                64 * 1024 * 1024,
                [&](uint32_t i) -> std::expected<void, Error>
                {
                    auto& texture = result.images[i];
                    if (texture.sourceMipNum != 0)
                    {
                        const auto& image = scene.images[texture.sourceImage];
                        const bool  srgb =
                            texture.format == TextureFormat::eRgba8Srgb || texture.format == TextureFormat::eBgra8Srgb;
                        TextureImportOptions sourceOptions;
                        sourceOptions.mipmaps = texture.sourceMipNum > 1;
                        sourceOptions.compression =
                            image.dds.empty() ? TextureCompression::eNone : options.textures.compression;
                        auto authored = prepareTexture(image, srgb, sourceOptions);
                        if (!authored)
                        {
                            return std::unexpected(authored.error());
                        }
                        if (authored->format != texture.format || authored->levels.size() != texture.sourceMipNum)
                        {
                            return makeError("Cached texture no longer matches its source");
                        }
                        for (uint32_t mip = 0; mip < texture.sourceMipNum; ++mip)
                        {
                            if (authored->levels[mip].size != texture.levels[mip].size)
                            {
                                return makeError("Cached dimensions no longer match the source");
                            }
                            texture.levels[mip].bytes = std::move(authored->levels[mip].bytes);
                        }
                    }
                    for (uint32_t mip = texture.sourceMipNum; mip < texture.levels.size(); ++mip)
                    {
                        const auto bytes = payloads[i][mip];
                        texture.levels[mip].bytes.assign(bytes.begin(), bytes.end());
                    }
                    return {};
                });
            if (!restored)
            {
                return std::unexpected(restored.error());
            }
            return result;
        }

        // Every failure here means the cache is stale or damaged; the caller reimports.
        std::expected<void, Error>
        validateCache(const CacheTarget& target, const AssetImportOptions& options, const AssetSource* assetSource)
        {
            const auto bytes = readFile(target.cachePath);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            CacheReader archive(*bytes);
            const auto  metadata = readMetadata(archive, *bytes, target.recipe);
            if (!metadata)
            {
                return std::unexpected(metadata.error());
            }
            const auto dependencies = metadata->find("dependencies");
            if (dependencies == metadata->end())
            {
                return makeError("Asset cache has no source dependencies");
            }
            return validateDependencies(*dependencies, options.workers, assetSource);
        }

        // Restores derived textures when the cache matches the loaded source content; failures mean reimport.
        std::expected<PreparedTextures, Error> readCache(const CacheTarget&        target,
                                                         const Json&               dependencies,
                                                         const SceneData&          scene,
                                                         const AssetImportOptions& options)
        {
            const auto bytes = readFile(target.cachePath);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            CacheReader archive(*bytes);
            const auto  metadata = readMetadata(archive, *bytes, target.recipe);
            if (!metadata)
            {
                return std::unexpected(metadata.error());
            }
            const auto cached = metadata->find("dependencies");
            if (cached == metadata->end() || *cached != dependencies)
            {
                return makeError("Changed asset source dependencies");
            }
            return readTextures(archive, scene, options);
        }

        std::expected<size_t, Error> writeCache(const CacheTarget&        target,
                                                const Json&               dependencies,
                                                const PreparedTextures&   textures,
                                                const AssetImportOptions& options)
        {
            const auto metadata = dumpJson(Json {{"recipe", target.recipe}, {"dependencies", dependencies}});
            if (!metadata)
            {
                return std::unexpected(metadata.error());
            }
            CacheWriter archive;
            archive.value(kMagic);
            archive.value(kPipelineVersion);
            const auto checksumOffset = archive.data.size();
            archive.value(uint64_t(0));
            const auto payloadOffset = archive.data.size();
            archive.array(std::span<const char>(*metadata));
            writeTextures(archive, textures);
            const auto checksum = XXH3_64bits(archive.data.data() + payloadOffset, archive.data.size() - payloadOffset);
            std::memcpy(archive.data.data() + checksumOffset, &checksum, sizeof(checksum));
            std::error_code error;
            std::filesystem::create_directories(options.cacheDirectory, error);
            if (error)
            {
                return makeError("Create asset cache directory {}: {}",
                                 pathText(options.cacheDirectory),
                                 error.message());
            }
            if (auto result = writeFileAtomically(target.cachePath, archive.data); !result)
            {
                return std::unexpected(result.error());
            }
            return archive.data.size();
        }
    } // namespace

    std::expected<bool, Error> isAssetCacheCurrent(const std::filesystem::path& input,
                                                   const AssetImportOptions&    options,
                                                   const AssetSource*           assetSource)
    {
        if (!options.cache)
        {
            return false;
        }
        const auto target = cacheTarget(input, options, assetSource);
        if (!target)
        {
            return std::unexpected(target.error());
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(target->cachePath, error))
        {
            return false;
        }
        if (auto result = validateCache(*target, options, assetSource); !result)
        {
            Logger::core().info("Asset cache needs import: {}: {}", pathText(input), result.error().message);
            return false;
        }
        return true;
    }

    std::expected<ImportedAsset, Error>
    importAsset(const std::filesystem::path& input, const AssetImportOptions& options, const AssetSource* assetSource)
    {
        const auto started = std::chrono::steady_clock::now();
        const auto target  = cacheTarget(input, options, assetSource);
        if (!target)
        {
            return std::unexpected(target.error());
        }
        const auto& source = target->source;
        Logger::core().info("Importing asset: {}", pathText(input));
        Logger::core().info("Loading geometry, materials and source images...");
        const auto                         loadStarted = std::chrono::steady_clock::now();
        auto                               lastReadLog = loadStarted;
        std::map<std::string, std::string> files;
        const SourceObserver               observer =
            [&files, &lastReadLog, assetSource](const auto&                path,
                                                std::span<const std::byte> bytes) -> std::expected<void, Error>
        {
            const auto name = cacheName(path, assetSource);
            if (!name)
            {
                return std::unexpected(name.error());
            }
            const auto hash              = hashBytes(bytes);
            const auto [entry, inserted] = files.emplace(*name, hash);
            const auto now               = std::chrono::steady_clock::now();
            if (now - lastReadLog >= std::chrono::milliseconds(500))
            {
                Logger::core().info("Read {} source files; {}", files.size(), pathText(path.filename()));
                lastReadLog = now;
            }
            if (!inserted && entry->second != hash)
            {
                return makeError("Asset changed while importing: {}", *name);
            }
            return {};
        };
        ImportedAsset asset;
        auto          extension = source.extension().string();
        std::ranges::transform(extension,
                               extension.begin(),
                               [](unsigned char value)
                               {
                                   return char(std::tolower(value));
                               });
        std::expected<SceneData, Error> scene;
        if (extension == ".gltf" || extension == ".glb")
        {
            scene = loadGltf(source, observer, options.workers, assetSource);
        }
        else if (extension == ".obj")
        {
            scene = loadObj(source, observer, options.workers, assetSource);
        }
        else if (extension == ".fbx")
        {
            scene = loadFbx(source, observer, options.workers, assetSource, options.fbx);
        }
        else
        {
            return makeError("Unsupported asset extension: {}", extension);
        }
        if (!scene)
        {
            return std::unexpected(scene.error());
        }
        asset.scene = std::move(*scene);
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
            const std::filesystem::path file = std::u8string(path.begin(), path.end());
            asset.dependencies.push_back({assetSource ? assetSource->root() / file : file, hash});
        }
        std::error_code cacheError;
        if (options.cache && !options.reimport && std::filesystem::exists(target->cachePath, cacheError))
        {
            Logger::core().info("Checking derived cache against loaded source content: {}", pathText(input));
            auto textures = readCache(*target, dependencies, asset.scene, options);
            if (textures)
            {
                asset.textures = std::move(*textures);
                asset.scene.images.clear();
                asset.cacheHit  = true;
                asset.cachePath = target->cachePath;
                Logger::core().info(
                    "Asset cache hit: {} ({:.1f} ms)",
                    pathText(input),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
                return asset;
            }
            Logger::core().info("Reimport {}: {}", pathText(input), textures.error().message);
        }
        if (cacheError)
        {
            return makeError("Query asset cache {}: {}", pathText(target->cachePath), cacheError.message());
        }
        const auto textureStarted = std::chrono::steady_clock::now();
        auto       textures       = prepareTextures(asset.scene, options.textures, options.workers);
        if (!textures)
        {
            return std::unexpected(textures.error());
        }
        asset.textures = std::move(*textures);
        Logger::core().info(
            "Texture preparation complete ({:.1f} ms)",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - textureStarted).count());
        asset.scene.images.clear();
        // Do not publish a mixture of source revisions if an editor changed a dependency during import.
        Logger::core().info("Verifying {} source dependencies before publishing...", files.size());
        if (auto result = validateDependencies(dependencies, options.workers, assetSource); !result)
        {
            return std::unexpected(result.error());
        }
        asset.cachePath = target->cachePath;
        if (options.cache)
        {
            Logger::core().info("Writing asset cache: {}", pathText(target->cachePath));
            const auto writeStarted = std::chrono::steady_clock::now();
            const auto written      = writeCache(*target, dependencies, asset.textures, options);
            if (written)
            {
                Logger::core().info(
                    "Asset cache written: {:.1f} MiB ({:.1f} ms)",
                    double(*written) / (1024 * 1024),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - writeStarted).count());
            }
            else
            {
                Logger::core().warn("Asset imported, but cache could not be written: {}", written.error().message);
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
