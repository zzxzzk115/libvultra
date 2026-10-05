#include <vultra/assets/project_manifest.hpp>
#include <vultra/assets/shader_asset.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <xxhash.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace vultra
{
    namespace
    {
        constexpr std::array<char, 8> kMagic {'V', 'U', 'L', 'T', 'R', 'A', 'P', 'K'};
        constexpr uint32_t            kVersion    = 1;
        constexpr uint64_t            kHeaderSize = 24;
        constexpr uint32_t            kMaxEntries = 100000;
        constexpr uint32_t            kMaxPath    = 4096;
        // Executable trailer: magic, version, reserved, archive size, XXH3 of the full VPK.
        constexpr std::array<char, 8> kEmbeddedMagic {'V', 'U', 'L', 'T', 'R', 'A', 'E', 'M'};
        constexpr uint32_t            kEmbeddedVersion    = 1;
        constexpr uint64_t            kEmbeddedFooterSize = 32;

        void writeNumber(std::ostream& output, uint64_t value, int bytes)
        {
            for (int i = 0; i < bytes; ++i)
            {
                output.put(char(value & 0xff));
                value >>= 8;
            }
            if (!output)
            {
                throw std::runtime_error("Write VPK number");
            }
        }

        uint64_t readNumber(std::istream& input, int bytes)
        {
            uint64_t value = 0;
            for (int i = 0; i < bytes; ++i)
            {
                const int byte = input.get();
                if (byte == EOF)
                {
                    throw std::runtime_error("Truncated VPK index");
                }
                value |= uint64_t(uint8_t(byte)) << (8 * i);
            }
            return value;
        }

        std::string pathText(const std::filesystem::path& path)
        {
            const auto text = path.generic_u8string();
            return {text.begin(), text.end()};
        }

        std::string archivePath(std::string_view text)
        {
            const std::u8string utf8(text.begin(), text.end());
            const auto          path = std::filesystem::path(utf8);
            if (text.empty() || text.find(char(0)) != std::string_view::npos ||
                text.find('\\') != std::string_view::npos || path.is_absolute() || path.has_root_name())
            {
                throw std::invalid_argument("VPK path must be relative");
            }
            for (const auto& part : path)
            {
                if (part == "..")
                {
                    throw std::invalid_argument("VPK path may not leave the project");
                }
            }
            const auto normalized = pathText(path.lexically_normal());
            if (normalized == "." || normalized != text || normalized.size() > kMaxPath)
            {
                throw std::invalid_argument("VPK path is not canonical");
            }
            return normalized;
        }

        std::filesystem::path sourceFile(const std::filesystem::path& root, std::string_view path)
        {
            const auto relative = archivePath(path);
            const auto resolved = std::filesystem::canonical(root / relative);
            const auto within   = resolved.lexically_relative(root);
            if (within.empty() || within.is_absolute())
            {
                throw std::invalid_argument("Project asset leaves its root: " + relative);
            }
            for (const auto& part : within)
            {
                if (part == "..")
                {
                    throw std::invalid_argument("Project asset leaves its root: " + relative);
                }
            }
            if (!std::filesystem::is_regular_file(resolved))
            {
                throw std::invalid_argument("Project asset is not a file: " + relative);
            }
            return resolved;
        }

        uint64_t offset(std::ostream& output)
        {
            const auto position = output.tellp();
            if (position < 0)
            {
                throw std::runtime_error("Query VPK output offset");
            }
            return uint64_t(position);
        }

        struct TempOutput
        {
            std::filesystem::path path;

            ~TempOutput()
            {
                if (!path.empty())
                {
                    std::error_code error;
                    std::filesystem::remove(path, error);
                }
            }
        };

        uint64_t transferAndHash(std::istream& input, uint64_t size, std::ostream* output = nullptr)
        {
            std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> hash(XXH3_createState(), &XXH3_freeState);
            if (!hash || XXH3_64bits_reset(hash.get()) == XXH_ERROR)
            {
                throw std::runtime_error("Initialize embedded VPK checksum");
            }
            std::array<char, 65536> buffer {};
            while (size > 0)
            {
                const auto count = std::streamsize(std::min<uint64_t>(size, buffer.size()));
                input.read(buffer.data(), count);
                if (!input)
                {
                    throw std::runtime_error("Read embedded VPK payload");
                }
                if (output)
                {
                    output->write(buffer.data(), count);
                    if (!*output)
                    {
                        throw std::runtime_error("Write embedded VPK payload");
                    }
                }
                if (XXH3_64bits_update(hash.get(), buffer.data(), size_t(count)) == XXH_ERROR)
                {
                    throw std::runtime_error("Hash embedded VPK payload");
                }
                size -= uint64_t(count);
            }
            return XXH3_64bits_digest(hash.get());
        }
    } // namespace

    VpkArchive::VpkArchive(std::filesystem::path file) :
        m_File(std::move(file))
    {
        load(0, std::filesystem::file_size(m_File));
    }

    VpkArchive::VpkArchive(std::filesystem::path file, uint64_t offset, uint64_t length) :
        m_File(std::move(file))
    {
        load(offset, length);
    }

    void VpkArchive::load(uint64_t offset, uint64_t length)
    {
        std::ifstream input(m_File, std::ios::binary | std::ios::ate);
        if (!input)
        {
            throw std::runtime_error("Open VPK: " + m_File.string());
        }
        const auto fileLength = input.tellg();
        if (fileLength < 0 || offset > uint64_t(fileLength) || length > uint64_t(fileLength) - offset ||
            length < kHeaderSize)
        {
            throw std::runtime_error("VPK bounds or header are invalid");
        }
        m_Offset = offset;
        input.seekg(std::streamoff(offset));
        std::array<char, 8> magic {};
        input.read(magic.data(), std::streamsize(magic.size()));
        if (magic != kMagic || readNumber(input, 4) != kVersion)
        {
            throw std::runtime_error("Unsupported VPK format or version");
        }
        const auto count       = readNumber(input, 4);
        const auto indexOffset = readNumber(input, 8);
        if (count > kMaxEntries || indexOffset < kHeaderSize || indexOffset > length)
        {
            throw std::runtime_error("Invalid VPK index bounds");
        }
        input.seekg(std::streamoff(offset + indexOffset));
        m_Entries.reserve(size_t(count));
        for (uint64_t i = 0; i < count; ++i)
        {
            const auto pathSize = readNumber(input, 4);
            if (pathSize == 0 || pathSize > kMaxPath || input.tellg() < 0 ||
                uint64_t(input.tellg()) - offset + pathSize + 24 > length)
            {
                throw std::runtime_error("Invalid VPK entry path size");
            }
            std::string path(size_t(pathSize), '\0');
            input.read(path.data(), std::streamsize(pathSize));
            const auto normalized = archivePath(path);
            Entry      entry {normalized, readNumber(input, 8), readNumber(input, 8), readNumber(input, 8)};
            if (entry.offset < kHeaderSize || entry.offset > indexOffset || entry.size > indexOffset - entry.offset ||
                std::ranges::any_of(m_Entries,
                                    [&](const Entry& existing)
                                    {
                                        return existing.path == entry.path;
                                    }))
            {
                throw std::runtime_error("Invalid or duplicate VPK entry: " + entry.path);
            }
            m_Entries.push_back(std::move(entry));
        }
    }

    const VpkArchive::Entry& VpkArchive::entry(std::string_view path) const
    {
        const auto normalized = archivePath(path);
        const auto found      = std::ranges::find_if(m_Entries,
                                                [&](const Entry& candidate)
                                                {
                                                    return candidate.path == normalized;
                                                });
        if (found == m_Entries.end())
        {
            throw std::invalid_argument("VPK entry not found: " + normalized);
        }
        return *found;
    }

    bool VpkArchive::contains(std::string_view path) const
    {
        const auto normalized = archivePath(path);
        return std::ranges::any_of(m_Entries,
                                   [&](const Entry& candidate)
                                   {
                                       return candidate.path == normalized;
                                   });
    }

    std::vector<std::byte> VpkArchive::read(std::string_view path) const
    {
        const auto& selected = entry(path);
        if (selected.size > std::numeric_limits<size_t>::max() ||
            selected.size > uint64_t(std::numeric_limits<std::streamsize>::max()))
        {
            throw std::runtime_error("VPK entry is too large: " + selected.path);
        }
        std::ifstream input(m_File, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Open VPK entry: " + selected.path);
        }
        input.seekg(std::streamoff(m_Offset + selected.offset));
        std::vector<std::byte> bytes;
        bytes.resize(size_t(selected.size));
        input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!input || XXH3_64bits(bytes.data(), bytes.size()) != selected.hash)
        {
            throw std::runtime_error("VPK entry read or checksum failed: " + selected.path);
        }
        return bytes;
    }

    void VpkArchive::extractTo(const std::filesystem::path& directory) const
    {
        if (std::filesystem::exists(directory))
        {
            throw std::invalid_argument("VPK extraction requires a fresh directory");
        }
        std::filesystem::create_directories(directory);
        try
        {
            for (const auto& selected : m_Entries)
            {
                const auto path = directory / selected.path;
                std::filesystem::create_directories(path.parent_path());
                const auto bytes = read(selected.path);
                writeFileAtomically(path, bytes);
            }
        }
        catch (...)
        {
            std::filesystem::remove_all(directory);
            throw;
        }
    }

    namespace
    {
        void packFiles(std::vector<std::pair<std::string, std::filesystem::path>>& files,
                       const std::filesystem::path&                                output)
        {
            if (std::filesystem::exists(output))
            {
                throw std::invalid_argument("VPK output already exists: " + output.string());
            }
            std::ranges::sort(files,
                              [](const auto& a, const auto& b)
                              {
                                  return a.first < b.first;
                              });
            for (size_t i = 1; i < files.size(); ++i)
            {
                if (files[i - 1].first == files[i].first && files[i - 1].second != files[i].second)
                {
                    throw std::invalid_argument("Conflicting VPK entry path: " + files[i].first);
                }
            }
            files.erase(std::unique(files.begin(),
                                    files.end(),
                                    [](const auto& a, const auto& b)
                                    {
                                        return a.first == b.first;
                                    }),
                        files.end());
            if (files.size() > kMaxEntries)
            {
                throw std::invalid_argument("Too many VPK entries");
            }
            TempOutput    temp {output.string() + ".tmp-" + StableId::generate().toString()};
            std::ofstream packed(temp.path, std::ios::binary | std::ios::trunc);
            if (!packed)
            {
                throw std::runtime_error("Create VPK: " + temp.path.string());
            }
            packed.write(kMagic.data(), std::streamsize(kMagic.size()));
            writeNumber(packed, kVersion, 4);
            writeNumber(packed, files.size(), 4);
            writeNumber(packed, 0, 8);

            struct PackedEntry
            {
                std::string path;
                uint64_t    offset;
                uint64_t    size;
                uint64_t    hash;
            };

            std::vector<PackedEntry> entries;
            std::array<char, 65536>  buffer {};
            for (const auto& [name, path] : files)
            {
                std::ifstream input(path, std::ios::binary);
                if (!input)
                {
                    throw std::runtime_error("Read VPK source: " + path.string());
                }
                std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> hash(XXH3_createState(), &XXH3_freeState);
                if (!hash || XXH3_64bits_reset(hash.get()) == XXH_ERROR)
                {
                    throw std::runtime_error("Initialize VPK checksum");
                }
                const uint64_t start = offset(packed);
                while (input)
                {
                    input.read(buffer.data(), std::streamsize(buffer.size()));
                    const auto count = input.gcount();
                    packed.write(buffer.data(), count);
                    if (!packed || XXH3_64bits_update(hash.get(), buffer.data(), size_t(count)) == XXH_ERROR)
                    {
                        throw std::runtime_error("Write VPK entry: " + name);
                    }
                }
                if (!input.eof())
                {
                    throw std::runtime_error("Read VPK source: " + path.string());
                }
                entries.push_back({name, start, offset(packed) - start, XXH3_64bits_digest(hash.get())});
            }

            const uint64_t indexOffset = offset(packed);
            for (const auto& entry : entries)
            {
                writeNumber(packed, entry.path.size(), 4);
                packed.write(entry.path.data(), std::streamsize(entry.path.size()));
                writeNumber(packed, entry.offset, 8);
                writeNumber(packed, entry.size, 8);
                writeNumber(packed, entry.hash, 8);
            }
            packed.seekp(16);
            writeNumber(packed, indexOffset, 8);
            packed.close();
            if (!packed)
            {
                throw std::runtime_error("Finalize VPK: " + temp.path.string());
            }
            std::filesystem::rename(temp.path, output);
            temp.path.clear();
        }
    } // namespace

    void VpkArchive::packProject(const std::filesystem::path& projectFile,
                                 const std::filesystem::path& output,
                                 const ShaderCompileOptions&  shaders)
    {
        auto       project = ProjectManifest::load(projectFile);
        const auto root    = std::filesystem::canonical(projectFile.parent_path().empty() ? std::filesystem::path(".") :
                                                                                         projectFile.parent_path());
        auto       scene   = SceneTree::load(sourceFile(root, pathText(project.mainScene)));
        scene.validateAssets(project);
        std::vector<std::pair<std::string, std::filesystem::path>> files;

        struct CookDirectory
        {
            std::filesystem::path path;

            ~CookDirectory()
            {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cooked {std::filesystem::temp_directory_path() /
                  ("vultra-project-shaders-" + StableId::generate().toString())};

        std::filesystem::create_directory(cooked.path);
        // Cook only explicit shader assets. Native include libraries are not project material entrypoints.
        const auto assets = project.assets();
        for (const auto& asset : assets)
        {
            if (asset.path.extension() == ".vshader" || asset.path.extension() == ".slang")
            {
                auto relative = asset.path;
                relative.replace_extension(".vshaderc");
                auto options = shaders;
                options.includeDirectories.insert(options.includeDirectories.begin(), root);
                const auto cached = root / ".vultra/shaders" / (asset.id.value.toString() + ".vshaderc");
                if (asset.path.extension() == ".vshader")
                {
                    ShaderAsset::cook(sourceFile(root, pathText(asset.path)), cached, options);
                }
                else
                {
                    ShaderProgram::cook(sourceFile(root, pathText(asset.path)), cached, options);
                }
                std::filesystem::create_directories((cooked.path / relative).parent_path());
                std::filesystem::copy_file(cached, cooked.path / relative);
                project.renameAsset(asset.id, relative);
            }
        }
        const auto manifest = cooked.path / "project.vproject";
        project.save(manifest);
        files.emplace_back("project.vproject", manifest);
        files.emplace_back(archivePath(pathText(project.mainScene)), sourceFile(root, pathText(project.mainScene)));
        for (const auto& asset : project.assets())
        {
            const auto artifact = cooked.path / asset.path;
            files.emplace_back(archivePath(pathText(asset.path)),
                               std::filesystem::is_regular_file(artifact) ? artifact :
                                                                            sourceFile(root, pathText(asset.path)));
        }
        for (const auto& extension : project.extensions)
        {
            files.emplace_back(archivePath(pathText(extension)), sourceFile(root, pathText(extension)));
        }
        for (const auto& script : project.scripts)
        {
            const auto path = archivePath(pathText(script.path));
            if (std::ranges::none_of(files,
                                     [&](const auto& file)
                                     {
                                         return file.first == path;
                                     }))
            {
                files.emplace_back(path, sourceFile(root, pathText(script.path)));
            }
            if (script.language == ScriptModule::Language::eCSharp)
            {
                const auto bridge = script.path.parent_path() / "Vultra.ManagedHost.dll";
                const auto api    = script.path.parent_path() / "Vultra.Scripting.dll";
                const auto config = std::filesystem::path(bridge).replace_extension(".runtimeconfig.json");
                files.emplace_back(archivePath(pathText(bridge)), sourceFile(root, pathText(bridge)));
                files.emplace_back(archivePath(pathText(api)), sourceFile(root, pathText(api)));
                files.emplace_back(archivePath(pathText(config)), sourceFile(root, pathText(config)));
                for (const auto& assembly : {script.path, bridge})
                {
                    const auto dependencies = std::filesystem::path(assembly).replace_extension(".deps.json");
                    if (std::filesystem::exists(root / dependencies))
                    {
                        files.emplace_back(archivePath(pathText(dependencies)),
                                           sourceFile(root, pathText(dependencies)));
                    }
                }
            }
        }
        packFiles(files, output);
    }

    void VpkArchive::embedProject(const std::filesystem::path& executable,
                                  const std::filesystem::path& projectPack,
                                  const std::filesystem::path& output)
    {
        if (std::filesystem::exists(output))
        {
            throw std::invalid_argument("Executable output already exists: " + output.string());
        }
        if (embeddedProject(executable))
        {
            throw std::invalid_argument("Executable already contains a project VPK");
        }
        VpkArchive archive(projectPack);
        if (!archive.contains("project.vproject"))
        {
            throw std::invalid_argument("Project VPK has no project manifest");
        }
        archive.read("project.vproject");

        TempOutput temp {output.string() + ".tmp-" + StableId::generate().toString()};
        const auto permissions = std::filesystem::status(executable).permissions();
        std::filesystem::copy_file(executable, temp.path);
        std::filesystem::permissions(temp.path,
                                     std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add);
        std::ifstream input(projectPack, std::ios::binary | std::ios::ate);
        std::ofstream packed(temp.path, std::ios::binary | std::ios::app);
        if (!input || !packed)
        {
            throw std::runtime_error("Open executable or project VPK for embedding");
        }
        const auto packSize = input.tellg();
        if (packSize < 0)
        {
            throw std::runtime_error("Query project VPK size");
        }
        input.seekg(0);
        const auto hash = transferAndHash(input, uint64_t(packSize), &packed);
        packed.write(kEmbeddedMagic.data(), std::streamsize(kEmbeddedMagic.size()));
        writeNumber(packed, kEmbeddedVersion, 4);
        writeNumber(packed, 0, 4);
        writeNumber(packed, uint64_t(packSize), 8);
        writeNumber(packed, hash, 8);
        packed.close();
        if (!packed)
        {
            throw std::runtime_error("Finalize embedded project executable");
        }
        std::filesystem::permissions(temp.path, permissions, std::filesystem::perm_options::replace);
        std::filesystem::rename(temp.path, output);
        temp.path.clear();
    }

    std::optional<VpkArchive> VpkArchive::embeddedProject(const std::filesystem::path& executable)
    {
        std::ifstream input(executable, std::ios::binary | std::ios::ate);
        if (!input)
        {
            throw std::runtime_error("Open executable for embedded VPK: " + executable.string());
        }
        const auto fileLength = input.tellg();
        if (fileLength < std::streamoff(kEmbeddedFooterSize))
        {
            return std::nullopt;
        }
        input.seekg(fileLength - std::streamoff(kEmbeddedFooterSize));
        std::array<char, 8> magic {};
        input.read(magic.data(), std::streamsize(magic.size()));
        if (magic != kEmbeddedMagic)
        {
            return std::nullopt;
        }
        const auto version  = readNumber(input, 4);
        const auto reserved = readNumber(input, 4);
        const auto packSize = readNumber(input, 8);
        const auto expected = readNumber(input, 8);
        if (version != kEmbeddedVersion || reserved != 0 || packSize < kHeaderSize ||
            packSize > uint64_t(fileLength) - kEmbeddedFooterSize)
        {
            throw std::runtime_error("Invalid embedded project VPK footer");
        }
        const auto packOffset = uint64_t(fileLength) - kEmbeddedFooterSize - packSize;
        input.seekg(std::streamoff(packOffset));
        if (transferAndHash(input, packSize) != expected)
        {
            throw std::runtime_error("Embedded project VPK checksum failed");
        }
        VpkArchive archive(executable, packOffset, packSize);
        if (!archive.contains("project.vproject"))
        {
            throw std::runtime_error("Embedded project VPK has no project manifest");
        }
        return archive;
    }

    void VpkArchive::packBuiltins(const std::filesystem::path& engineRoot, const std::filesystem::path& output)
    {
        const auto engine = std::filesystem::canonical(engineRoot);
        const auto passes = engine / "builtin/shaders/passes";
        if (!std::filesystem::is_directory(passes))
        {
            throw std::invalid_argument("Engine shader directory is missing: " + passes.string());
        }

        // The cook directory belongs to this invocation and is removed after the archive is finalized.
        struct CookDirectory
        {
            std::filesystem::path path;

            ~CookDirectory()
            {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cooked {std::filesystem::temp_directory_path() / ("vultra-shaders-" + StableId::generate().toString())};

        std::filesystem::create_directory(cooked.path);
        const std::array                   includes {engine / "builtin/shaders", engine / "external"};
        std::vector<std::filesystem::path> sources;
        for (const auto& item : std::filesystem::recursive_directory_iterator(passes))
        {
            if (item.is_regular_file() && item.path().extension() == ".slang")
            {
                sources.push_back(item.path());
            }
        }
        std::ranges::sort(sources);
        if (sources.empty())
        {
            throw std::invalid_argument("No built-in shader passes to cook");
        }
        std::vector<std::pair<std::string, std::filesystem::path>> files;
        for (const auto& source : sources)
        {
            Logger::core().info("Cooking built-in shader {}", source.filename().string());
            const auto program = ShaderProgram::compile(source, {}, includes, source.filename() == "path_trace.slang");
            if (!program.diagnostics.empty())
            {
                Logger::core().warn("{}", program.diagnostics);
            }
            auto relative = source.lexically_relative(engine);
            relative.replace_extension(".vshaderc");
            const auto outputFile = cooked.path / relative;
            program.save(outputFile);
            files.emplace_back(archivePath(pathText(relative)), outputFile);
        }
        // Ship upstream attribution with the compiled implementation, without compiler inputs.
        for (const auto* name : {"LICENSE", "README.vultra.md"})
        {
            const auto relative = std::string("external/openpbr/") + name;
            files.emplace_back(relative, sourceFile(engine, relative));
        }
        packFiles(files, output);
    }
} // namespace vultra
