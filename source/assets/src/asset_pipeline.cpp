#include "asset_file.hpp"
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
#include <stdexcept>

namespace vultra
{
    namespace
    {
        using asset_detail::CacheReader;
        using asset_detail::CacheWriter;
        using Json = nlohmann::json;

        constexpr uint64_t kMagic = 0x0054455353414c56; // "VLASSET\0", little endian.
        // Pre-release cache format stays at 1; the import recipe identifies preparation semantics.
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

        Json importRecipe(const std::filesystem::path& source, const AssetImportOptions& options)
        {
            Json recipe {{"version", kPipelineVersion},
                         {"source", pathText(source)},
                         {"mipmaps", options.textures.mipmaps},
                         {"compression", int(options.textures.compression)},
                         {"bc7_encoder", "bc7e-ispc-fast-explicit-decrement-v1"},
                         {"dds_srgb", "preserve-bc-v1"}};
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

        std::filesystem::path
        cacheFilePath(const std::filesystem::path& source, const Json& recipe, const AssetImportOptions& options)
        {
            const auto text = recipe.dump();
            const auto key  = hashBytes(std::as_bytes(std::span(text)));
            auto       name = source.stem();
            name += "-" + key + ".vasset";
            return options.cacheDirectory / name;
        }

        std::expected<std::string, std::string> hashFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                return std::unexpected("Missing asset dependency: " + pathText(path));
            }
            std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state(XXH3_createState(), &XXH3_freeState);
            if (!state || XXH3_64bits_reset(state.get()) == XXH_ERROR)
            {
                return std::unexpected("Initialize asset hash");
            }
            std::array<char, 65536> buffer;
            while (file)
            {
                file.read(buffer.data(), std::streamsize(buffer.size()));
                if (XXH3_64bits_update(state.get(), buffer.data(), size_t(file.gcount())) == XXH_ERROR)
                {
                    return std::unexpected("Hash asset dependency: " + pathText(path));
                }
            }
            if (file.bad() || !file.eof())
            {
                return std::unexpected("Read asset dependency: " + pathText(path));
            }
            return std::format("{:016x}", XXH3_64bits_digest(state.get()));
        }

        std::expected<std::filesystem::path, std::string> resolveSource(const std::filesystem::path& input,
                                                                        const AssetSource*           source)
        {
            if (source)
            {
                return source->resolveExpected(input);
            }
            if (input.native().find(std::filesystem::path::value_type(0)) != std::filesystem::path::string_type::npos)
            {
                return std::unexpected("Asset path contains a NUL character");
            }
            std::error_code error;
            auto            path = std::filesystem::canonical(input, error);
            if (error)
            {
                return std::unexpected("Resolve asset " + pathText(input) + ": " + error.message());
            }
            return path;
        }

        std::expected<void, std::string>
        validateDependencies(const Json& dependencies, uint32_t workers, const AssetSource* source)
        {
            if (!dependencies.is_array() || dependencies.empty() || dependencies.size() > UINT32_MAX)
            {
                return std::unexpected("Asset cache has invalid source dependencies");
            }
            return asset_detail::runImportJobsExpected(
                "Verifying source dependencies",
                uint32_t(dependencies.size()),
                workers,
                65536,
                [&](uint32_t i) -> std::expected<void, std::string>
                {
                    const auto& dependency = dependencies[i];
                    const auto  path       = dependency.find("path");
                    const auto  hash       = dependency.find("hash");
                    if (path == dependency.end() || !path->is_string() || hash == dependency.end() ||
                        !hash->is_string())
                    {
                        return std::unexpected("Invalid cached asset dependency");
                    }
                    const auto&                 name           = path->get_ref<const std::string&>();
                    const std::filesystem::path dependencyPath = std::u8string(name.begin(), name.end());
                    if (dependencyPath.native().find(std::filesystem::path::value_type(0)) !=
                        std::filesystem::path::string_type::npos)
                    {
                        return std::unexpected("Cached asset dependency path contains a NUL character");
                    }
                    std::string actual;
                    if (source)
                    {
                        auto bytes = source->readExpected(dependencyPath);
                        if (!bytes)
                        {
                            return std::unexpected(bytes.error());
                        }
                        actual = hashBytes(*bytes);
                    }
                    else
                    {
                        auto hashed = hashFile(dependencyPath);
                        if (!hashed)
                        {
                            return std::unexpected(hashed.error());
                        }
                        actual = std::move(*hashed);
                    }
                    if (actual != hash->get_ref<const std::string&>())
                    {
                        return std::unexpected("Changed asset dependency: " + name);
                    }
                    return {};
                });
        }

        std::expected<Json, std::string>
        readMetadata(CacheReader& archive, std::span<const std::byte> bytes, const Json& recipe)
        {
            uint64_t magic;
            uint32_t version;
            uint64_t checksum;
            if (!archive.read(magic) || !archive.read(version) || !archive.read(checksum) || magic != kMagic ||
                version != kPipelineVersion)
            {
                return std::unexpected("Invalid or unsupported asset cache header");
            }
            constexpr size_t prefix = sizeof(magic) + sizeof(version) + sizeof(checksum);
            if (XXH3_64bits(bytes.data() + prefix, bytes.size() - prefix) != checksum)
            {
                return std::unexpected("Asset cache checksum mismatch");
            }
            std::vector<char> text;
            if (!archive.readArray(text))
            {
                return std::unexpected("Invalid asset cache metadata length");
            }
            auto       metadata     = Json::parse(text, nullptr, false);
            const auto cachedRecipe = metadata.find("recipe");
            const auto dependencies = metadata.find("dependencies");
            if (cachedRecipe == metadata.end() || *cachedRecipe != recipe || dependencies == metadata.end() ||
                !dependencies->is_array())
            {
                return std::unexpected("Asset import recipe or dependency metadata changed");
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

        std::expected<PreparedTextures, std::string>
        readTextures(CacheReader& reader, const SceneData& scene, const AssetImportOptions& options)
        {
            PreparedTextures result;
            uint32_t         count;
            if (!reader.readArray(result.materials) || !reader.read(count) || count < 2 || count > 1000000 ||
                result.materials.size() != scene.materials.size())
            {
                return std::unexpected("Invalid cached texture/material count");
            }
            result.images.resize(count);
            std::vector<std::vector<std::span<const std::byte>>> payloads(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                auto& texture        = result.images[i];
                uint32_t levels;
                if (!reader.read(texture.format) || !reader.read(texture.sourceImage) ||
                    !reader.read(texture.sourceMipNum) || !reader.read(levels))
                {
                    return std::unexpected("Truncated cached texture header");
                }
                const auto layout = asset_detail::textureLayout(texture.format);
                if (!layout)
                {
                    return std::unexpected(layout.error());
                }
                if (levels == 0 || levels > 32 || texture.sourceMipNum > levels || texture.sourceImage < -1 ||
                    (texture.sourceImage >= 0 && size_t(texture.sourceImage) >= scene.images.size()) ||
                    (texture.sourceMipNum != 0 && texture.sourceImage < 0))
                {
                    return std::unexpected("Invalid cached texture source or mip count");
                }
                texture.levels.resize(levels);
                for (uint32_t mip = 0; mip < levels; ++mip)
                {
                    auto&                      level = texture.levels[mip];
                    std::span<const std::byte> pixels;
                    if (!reader.read(level.size) || !reader.readArrayBytes<std::byte>(pixels) || level.size.empty())
                    {
                        return std::unexpected("Invalid cached derived mip");
                    }
                    const auto blocksX = (uint64_t(level.size.width) + layout->block - 1) / layout->block;
                    const auto blocksY = (uint64_t(level.size.height) + layout->block - 1) / layout->block;
                    const auto blocks  = mip < texture.sourceMipNum ? 0 : blocksX * blocksY;
                    if (pixels.size() % layout->bytes != 0 || pixels.size() / layout->bytes != blocks)
                    {
                        return std::unexpected("Invalid cached derived mip size");
                    }
                    if (mip > 0)
                    {
                        const auto previous = texture.levels[mip - 1].size;
                        if (level.size != Extent {std::max(1u, previous.width / 2), std::max(1u, previous.height / 2)})
                        {
                            return std::unexpected("Invalid cached mip dimensions");
                        }
                    }
                    payloads[i].push_back(pixels);
                }
            }
            if (!reader.empty())
            {
                return std::unexpected("Trailing derived cache data");
            }
            for (const auto& material : result.materials)
            {
                for (auto image : material)
                {
                    if (image >= count)
                    {
                        return std::unexpected("Invalid cached material texture");
                    }
                }
            }
            auto restored = asset_detail::runImportJobsExpected(
                "Restoring derived textures",
                count,
                options.workers,
                64 * 1024 * 1024,
                [&](uint32_t i) -> std::expected<void, std::string>
                {
                    auto& texture = result.images[i];
                    if (texture.sourceMipNum != 0)
                    {
                        const auto& image = scene.images[texture.sourceImage];
                        const bool  srgb =
                            texture.format == TextureFormat::eRgba8Srgb ||
                            texture.format == TextureFormat::eBgra8Srgb || texture.format == TextureFormat::eBc1Srgb ||
                            texture.format == TextureFormat::eBc2Srgb || texture.format == TextureFormat::eBc3Srgb ||
                            texture.format == TextureFormat::eBc7Srgb;
                        TextureImportOptions sourceOptions;
                        sourceOptions.mipmaps = texture.sourceMipNum > 1;
                        sourceOptions.compression =
                            image.dds.empty() ? TextureCompression::eNone : options.textures.compression;
                        auto authored = prepareTextureExpected(image, srgb, sourceOptions);
                        if (!authored)
                        {
                            return std::unexpected(authored.error());
                        }
                        if (authored->format != texture.format || authored->levels.size() != texture.sourceMipNum)
                        {
                            return std::unexpected("Cached texture no longer matches its source");
                        }
                        for (uint32_t mip = 0; mip < texture.sourceMipNum; ++mip)
                        {
                            if (authored->levels[mip].size != texture.levels[mip].size)
                            {
                                return std::unexpected("Cached dimensions no longer match the source");
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

        std::expected<PreparedTextures, std::string> readDerivedCache(const std::filesystem::path& path,
                                                                      const Json&                  recipe,
                                                                      const Json&                  dependencies,
                                                                      const SceneData&             scene,
                                                                      const AssetImportOptions&    options)
        {
            const auto bytes = detail::readAssetFileExpected(path);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            CacheReader archive(*bytes);
            const auto  metadata = readMetadata(archive, *bytes, recipe);
            if (!metadata)
            {
                return std::unexpected(metadata.error());
            }
            if (*metadata->find("dependencies") != dependencies)
            {
                return std::unexpected("Changed asset source dependencies");
            }
            return readTextures(archive, scene, options);
        }

        std::expected<void, std::string> publishCache(const std::filesystem::path& path,
                                                      const Json&                  recipe,
                                                      const Json&                  dependencies,
                                                      const PreparedTextures&      textures)
        {
            const auto  metadata = Json {{"recipe", recipe}, {"dependencies", dependencies}}.dump();
            CacheWriter archive;
            archive.value(kMagic);
            archive.value(kPipelineVersion);
            const auto checksumOffset = archive.data.size();
            archive.value(uint64_t(0));
            const auto payloadOffset = archive.data.size();
            archive.array(std::span<const char>(metadata));
            writeTextures(archive, textures);
            const auto checksum = XXH3_64bits(archive.data.data() + payloadOffset, archive.data.size() - payloadOffset);
            std::memcpy(archive.data.data() + checksumOffset, &checksum, sizeof(checksum));
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
            {
                return std::unexpected("Create asset cache directory: " + error.message());
            }
            if (auto written = writeFileAtomicallyExpected(path, archive.data); !written)
            {
                return written;
            }
            Logger::core().info("Asset cache written: {:.1f} MiB", double(archive.data.size()) / (1024 * 1024));
            return {};
        }
    } // namespace

    bool isAssetCacheCurrent(const std::filesystem::path& input,
                             const AssetImportOptions&    options,
                             const AssetSource*           assetSource)
    {
        if (!options.cache)
        {
            return false;
        }
        const auto source = resolveSource(input, assetSource);
        if (!source)
        {
            return false;
        }
        const auto recipe    = importRecipe(*source, options);
        const auto cachePath = cacheFilePath(*source, recipe, options);
        const auto bytes     = detail::readAssetFileExpected(cachePath);
        if (!bytes)
        {
            return false;
        }
        CacheReader archive(*bytes);
        const auto  metadata = readMetadata(archive, *bytes, recipe);
        if (!metadata)
        {
            Logger::core().info("Asset cache needs import: {}: {}", pathText(input), metadata.error());
            return false;
        }
        const auto current = validateDependencies(*metadata->find("dependencies"), options.workers, assetSource);
        if (!current)
        {
            Logger::core().info("Asset cache needs import: {}: {}", pathText(input), current.error());
        }
        return current.has_value();
    }

    std::expected<ImportedAsset, std::string> importAssetExpected(const std::filesystem::path& input,
                                                                  const AssetImportOptions&    options,
                                                                  const AssetSource*           assetSource)
    {
        const auto started = std::chrono::steady_clock::now();
        const auto source  = resolveSource(input, assetSource);
        if (!source)
        {
            return std::unexpected(source.error());
        }
        const auto recipe    = importRecipe(*source, options);
        const auto cachePath = cacheFilePath(*source, recipe, options);
        Logger::core().info("Importing asset: {}", pathText(input));
        Logger::core().info("Loading geometry, materials and source images...");
        const auto                         loadStarted = std::chrono::steady_clock::now();
        auto                               lastReadLog = loadStarted;
        std::map<std::string, std::string> files;
        const ExpectedSourceObserver       observer =
            [&](const auto& path, std::span<const std::byte> bytes) -> std::expected<void, std::string>
        {
            auto resolved = resolveSource(path, assetSource);
            if (!resolved)
            {
                return std::unexpected(resolved.error());
            }
            const auto name              = pathText(*resolved);
            const auto hash = hashBytes(bytes);
            const auto [entry, inserted] = files.emplace(name, hash);
            const auto now               = std::chrono::steady_clock::now();
            if (now - lastReadLog >= std::chrono::milliseconds(500))
            {
                Logger::core().info("Read {} source files; {}", files.size(), pathText(path.filename()));
                lastReadLog = now;
            }
            if (!inserted && entry->second != hash)
            {
                return std::unexpected("Asset changed while importing: " + name);
            }
            return {};
        };
        auto extension = source->extension().string();
        std::ranges::transform(extension,
                               extension.begin(),
                               [](unsigned char value)
                               {
                                   return char(std::tolower(value));
                               });
        auto load = [&]() -> std::expected<SceneData, std::string>
        {
            if (extension == ".gltf" || extension == ".glb")
            {
                return loadGltfExpected(*source, observer, options.workers, assetSource);
            }
            if (extension == ".obj")
            {
                return loadObjExpected(*source, observer, options.workers, assetSource);
            }
            if (extension == ".fbx")
            {
                return loadFbxExpected(*source, observer, options.workers, assetSource, options.fbx);
            }
            return std::unexpected("Unsupported asset extension: " + extension);
        };
        auto scene = load();
        if (!scene)
        {
            return std::unexpected(scene.error());
        }
        ImportedAsset asset;
        asset.scene     = std::move(*scene);
        asset.cachePath = cachePath;
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
            asset.dependencies.push_back({std::filesystem::path(std::u8string(path.begin(), path.end())), hash});
        }
        std::error_code error;
        if (options.cache && !options.reimport && std::filesystem::is_regular_file(cachePath, error))
        {
            auto cached = readDerivedCache(cachePath, recipe, dependencies, asset.scene, options);
            if (cached)
            {
                // An edit during cache restoration must fail just like an edit during texture preparation.
                if (auto current = validateDependencies(dependencies, options.workers, assetSource); !current)
                {
                    return std::unexpected(current.error());
                }
                asset.textures = std::move(*cached);
                asset.scene.images.clear();
                asset.cacheHit = true;
                Logger::core().info(
                    "Asset cache hit: {} ({:.1f} ms)",
                    pathText(input),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
                return asset;
            }
            Logger::core().info("Reimport {}: {}", pathText(input), cached.error());
        }
        const auto textureStarted = std::chrono::steady_clock::now();
        auto       textures       = prepareTexturesExpected(asset.scene, options.textures, options.workers);
        if (!textures)
        {
            return std::unexpected(textures.error());
        }
        asset.textures = std::move(*textures);
        asset.scene.images.clear();
        Logger::core().info(
            "Texture preparation complete ({:.1f} ms)",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - textureStarted).count());
        if (auto current = validateDependencies(dependencies, options.workers, assetSource); !current)
        {
            return std::unexpected(current.error());
        }
        if (options.cache)
        {
            if (auto published = publishCache(cachePath, recipe, dependencies, asset.textures); !published)
            {
                Logger::core().warn("Asset imported, but cache could not be written: {}", published.error());
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

    ImportedAsset
    importAsset(const std::filesystem::path& input, const AssetImportOptions& options, const AssetSource* assetSource)
    {
        auto asset = importAssetExpected(input, options, assetSource);
        if (!asset)
        {
            if (asset.error().starts_with("Unsupported asset extension:") ||
                asset.error() == "Unknown FBX material convention")
            {
                throw std::invalid_argument(asset.error());
            }
            throw std::runtime_error(asset.error());
        }
        return std::move(*asset);
    }
} // namespace vultra
