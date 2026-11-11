#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/platform/os/file.hpp>

#include <xxhash.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
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

        struct NumberField
        {
            uint64_t value;
            int      bytes;
        };

        std::expected<void, Error> writeNumbers(std::ostream& output, std::initializer_list<NumberField> fields)
        {
            for (auto [value, bytes] : fields)
            {
                for (int i = 0; i < bytes; ++i)
                {
                    output.put(char(value & 0xff));
                    value >>= 8;
                }
            }
            if (!output)
            {
                return makeError("Write VPK number");
            }
            return {};
        }

        std::expected<uint64_t, Error> readNumber(std::istream& input, int bytes)
        {
            uint64_t value = 0;
            for (int i = 0; i < bytes; ++i)
            {
                const int byte = input.get();
                if (byte == EOF)
                {
                    return makeError("Truncated VPK index");
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

        std::expected<uint64_t, Error> offset(std::ostream& output)
        {
            const auto position = output.tellp();
            if (position < 0)
            {
                return makeError("Query VPK output offset");
            }
            return uint64_t(position);
        }

        std::expected<std::filesystem::path, Error> absolutePath(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto      result = std::filesystem::absolute(path, error);
            if (error)
            {
                return makeError("Resolve VPK path {}: {}", path.string(), error.message());
            }
            return result.lexically_normal();
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

        std::expected<uint64_t, Error>
        transferAndHash(std::istream& input, uint64_t size, std::ostream* output = nullptr)
        {
            std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> hash(XXH3_createState(), &XXH3_freeState);
            if (!hash || XXH3_64bits_reset(hash.get()) == XXH_ERROR)
            {
                return makeError("Initialize embedded VPK checksum");
            }
            // Keep package I/O buffers off the small default WebAssembly stack.
            std::vector<char> buffer(65536);
            while (size > 0)
            {
                const auto count = std::streamsize(std::min<uint64_t>(size, buffer.size()));
                input.read(buffer.data(), count);
                if (!input)
                {
                    return makeError("Read embedded VPK payload");
                }
                if (output)
                {
                    output->write(buffer.data(), count);
                    if (!*output)
                    {
                        return makeError("Write embedded VPK payload");
                    }
                }
                if (XXH3_64bits_update(hash.get(), buffer.data(), size_t(count)) == XXH_ERROR)
                {
                    return makeError("Hash embedded VPK payload");
                }
                size -= uint64_t(count);
            }
            return XXH3_64bits_digest(hash.get());
        }

        std::expected<bool, Error> pathExists(const std::filesystem::path& path)
        {
            std::error_code error;
            const bool      exists = std::filesystem::exists(path, error);
            if (error)
            {
                return makeError("Query {}: {}", path.string(), error.message());
            }
            return exists;
        }
    } // namespace

    std::expected<std::string, Error> VpkArchive::canonicalPath(std::string_view text)
    {
        const std::u8string utf8(text.begin(), text.end());
        const auto          path = std::filesystem::path(utf8);
        if (text.empty() || text.find(char(0)) != std::string_view::npos || text.find('\\') != std::string_view::npos ||
            path.is_absolute() || path.has_root_name() || path.has_root_directory())
        {
            return makeError("VPK path must be relative");
        }
        for (const auto& part : path)
        {
            if (part == "..")
            {
                return makeError("VPK path may not leave the project");
            }
        }
        const auto normalized = pathText(path.lexically_normal());
        if (normalized == "." || normalized != text || normalized.size() > kMaxPath)
        {
            return makeError("VPK path is not canonical");
        }
        return normalized;
    }

    VpkArchive::VpkArchive(std::filesystem::path file, std::span<const std::byte> bytes) :
        m_File(std::move(file)),
        m_Bytes(bytes)
    {
    }

    std::expected<VpkArchive, Error> VpkArchive::create(const std::filesystem::path& file)
    {
        auto path = absolutePath(file);
        if (!path)
        {
            return std::unexpected(path.error());
        }
        std::error_code error;
        const auto      length = std::filesystem::file_size(*path, error);
        if (error)
        {
            return makeError("Open VPK: {}: {}", path->string(), error.message());
        }
        VpkArchive archive(std::move(*path), {});
        if (auto result = archive.load(0, length); !result)
        {
            return std::unexpected(result.error());
        }
        return archive;
    }

    std::expected<VpkArchive, Error> VpkArchive::create(const std::filesystem::path& identity,
                                                        std::span<const std::byte>   bytes)
    {
        if (identity.empty() || bytes.size() < kHeaderSize)
        {
            return makeError("Embedded VPK requires an identity and a complete header");
        }
        auto path = absolutePath(identity);
        if (!path)
        {
            return std::unexpected(path.error());
        }
        VpkArchive archive(std::move(*path), bytes);
        if (auto result = archive.load(0, bytes.size()); !result)
        {
            return std::unexpected(result.error());
        }
        return archive;
    }

    std::expected<void, Error> VpkArchive::load(uint64_t offset, uint64_t length)
    {
        std::ifstream input;
        uint64_t      fileLength = m_Bytes.size();
        if (m_Bytes.empty())
        {
            input.open(m_File, std::ios::binary | std::ios::ate);
            if (!input || input.tellg() < 0)
            {
                return makeError("Open VPK: {}", m_File.string());
            }
            fileLength = uint64_t(input.tellg());
            input.seekg(std::streamoff(offset));
        }
        if (offset > fileLength || length > fileLength - offset || length < kHeaderSize)
        {
            return makeError("VPK bounds or header are invalid");
        }
        m_Offset          = offset;
        uint64_t position = 0;
        // One bounded parser for files, executable regions and immutable embedded bytes.
        const auto read = [&](std::span<char> output) -> std::expected<void, Error>
        {
            if (output.size() > length - position)
            {
                return makeError("Truncated VPK index");
            }
            if (!m_Bytes.empty())
            {
                std::memcpy(output.data(), m_Bytes.data() + size_t(offset + position), output.size());
            }
            else
            {
                input.read(output.data(), std::streamsize(output.size()));
                if (!input)
                {
                    return makeError("Read VPK index: {}", m_File.string());
                }
            }
            position += output.size();
            return {};
        };
        const auto number = [&](size_t count) -> std::expected<uint64_t, Error>
        {
            std::array<char, 8> data {};
            if (auto result = read(std::span(data).first(count)); !result)
            {
                return std::unexpected(result.error());
            }
            uint64_t value = 0;
            for (size_t i = 0; i < count; ++i)
            {
                value |= uint64_t(uint8_t(data[i])) << (8 * i);
            }
            return value;
        };
        std::array<char, 8> magic {};
        if (auto result = read(magic); !result)
        {
            return result;
        }
        const auto version = number(4);
        if (!version)
        {
            return std::unexpected(version.error());
        }
        if (magic != kMagic || *version != kVersion)
        {
            return makeError("Unsupported VPK format or version");
        }
        const auto count = number(4);
        if (!count)
        {
            return std::unexpected(count.error());
        }
        const auto indexOffset = number(8);
        if (!indexOffset)
        {
            return std::unexpected(indexOffset.error());
        }
        if (*count > kMaxEntries || *indexOffset < kHeaderSize || *indexOffset > length)
        {
            return makeError("Invalid VPK index bounds");
        }
        position = *indexOffset;
        if (input.is_open())
        {
            input.seekg(std::streamoff(offset + *indexOffset));
        }
        m_Entries.reserve(size_t(*count));
        for (uint64_t i = 0; i < *count; ++i)
        {
            const auto pathSize = number(4);
            if (!pathSize)
            {
                return std::unexpected(pathSize.error());
            }
            if (*pathSize == 0 || *pathSize > kMaxPath || *pathSize + 24 > length - position)
            {
                return makeError("Invalid VPK entry path size");
            }
            std::string path(size_t(*pathSize), '\0');
            if (auto result = read(path); !result)
            {
                return result;
            }
            auto normalized = VpkArchive::canonicalPath(path);
            if (!normalized)
            {
                return std::unexpected(normalized.error());
            }
            Entry entry {std::move(*normalized), 0, 0, 0};
            for (auto* field : {&entry.offset, &entry.size, &entry.hash})
            {
                const auto value = number(8);
                if (!value)
                {
                    return std::unexpected(value.error());
                }
                *field = *value;
            }
            if (entry.offset < kHeaderSize || entry.offset > *indexOffset || entry.size > *indexOffset - entry.offset ||
                std::ranges::any_of(m_Entries,
                                    [&](const Entry& existing)
                                    {
                                        return existing.path == entry.path;
                                    }))
            {
                return makeError("Invalid or duplicate VPK entry: {}", entry.path);
            }
            m_Entries.push_back(std::move(entry));
        }
        return {};
    }

    std::expected<const VpkArchive::Entry*, Error> VpkArchive::entry(std::string_view path) const
    {
        const auto normalized = VpkArchive::canonicalPath(path);
        if (!normalized)
        {
            return std::unexpected(normalized.error());
        }
        const auto found = std::ranges::find_if(m_Entries,
                                                [&](const Entry& candidate)
                                                {
                                                    return candidate.path == *normalized;
                                                });
        if (found == m_Entries.end())
        {
            return makeError("VPK entry not found: {}", *normalized);
        }
        return &*found;
    }

    const std::filesystem::path& VpkArchive::file() const
    {
        return m_File;
    }

    std::expected<bool, Error> VpkArchive::contains(std::string_view path) const
    {
        const auto normalized = VpkArchive::canonicalPath(path);
        if (!normalized)
        {
            return std::unexpected(normalized.error());
        }
        return std::ranges::any_of(m_Entries,
                                   [&](const Entry& candidate)
                                   {
                                       return candidate.path == *normalized;
                                   });
    }

    std::expected<uint64_t, Error> VpkArchive::size(std::string_view path) const
    {
        const auto selected = entry(path);
        if (!selected)
        {
            return std::unexpected(selected.error());
        }
        return (*selected)->size;
    }

    std::expected<std::vector<std::byte>, Error> VpkArchive::read(std::string_view path) const
    {
        const auto found = entry(path);
        if (!found)
        {
            return std::unexpected(found.error());
        }
        const auto& selected = **found;
        if (selected.size > std::numeric_limits<size_t>::max() ||
            selected.size > uint64_t(std::numeric_limits<std::streamsize>::max()))
        {
            return makeError("VPK entry is too large: {}", selected.path);
        }
        std::vector<std::byte> bytes(size_t(selected.size));
        if (!m_Bytes.empty())
        {
            const auto stored = m_Bytes.subspan(size_t(m_Offset + selected.offset), size_t(selected.size));
            std::ranges::copy(stored, bytes.begin());
        }
        else
        {
            std::ifstream input(m_File, std::ios::binary);
            if (!input)
            {
                return makeError("Open VPK entry: {}", selected.path);
            }
            input.seekg(std::streamoff(m_Offset + selected.offset));
            input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!input)
            {
                return makeError("Read VPK entry: {}", selected.path);
            }
        }
        if (XXH3_64bits(bytes.data(), bytes.size()) != selected.hash)
        {
            return makeError("VPK entry checksum failed: {}", selected.path);
        }
        return bytes;
    }

    std::expected<void, Error> VpkArchive::extractTo(const std::filesystem::path& directory) const
    {
        const auto exists = pathExists(directory);
        if (!exists)
        {
            return std::unexpected(exists.error());
        }
        if (*exists)
        {
            return makeError("VPK extraction requires a fresh directory");
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error)
        {
            return makeError("Create VPK extraction directory {}: {}", directory.string(), error.message());
        }
        const auto extract = [&]() -> std::expected<void, Error>
        {
            for (const auto& selected : m_Entries)
            {
                const auto      path = directory / selected.path;
                std::error_code directoryError;
                std::filesystem::create_directories(path.parent_path(), directoryError);
                if (directoryError)
                {
                    return makeError("Create directory {}: {}", path.parent_path().string(), directoryError.message());
                }
                const auto bytes = read(selected.path);
                if (!bytes)
                {
                    return std::unexpected(bytes.error());
                }
                if (auto result = writeFileAtomically(path, *bytes); !result)
                {
                    return result;
                }
            }
            return {};
        };
        if (auto result = extract(); !result)
        {
            // Leave no partial extraction behind.
            std::filesystem::remove_all(directory, error);
            return result;
        }
        return {};
    }

    std::expected<void, Error> VpkArchive::pack(std::span<const VpkFile>     inputFiles,
                                                const std::filesystem::path& output)
    {
        std::vector<VpkFile> files(inputFiles.begin(), inputFiles.end());
        for (auto& file : files)
        {
            auto path = VpkArchive::canonicalPath(file.path);
            if (!path)
            {
                return std::unexpected(path.error());
            }
            file.path = std::move(*path);
        }
        const auto exists = pathExists(output);
        if (!exists)
        {
            return std::unexpected(exists.error());
        }
        if (*exists)
        {
            return makeError("VPK output already exists: {}", output.string());
        }
        std::ranges::sort(files,
                          [](const auto& a, const auto& b)
                          {
                              return a.path < b.path;
                          });
        for (size_t i = 1; i < files.size(); ++i)
        {
            if (files[i - 1].path == files[i].path && files[i - 1].source != files[i].source)
            {
                return makeError("Conflicting VPK entry path: {}", files[i].path);
            }
        }
        files.erase(std::unique(files.begin(),
                                files.end(),
                                [](const auto& a, const auto& b)
                                {
                                    return a.path == b.path;
                                }),
                    files.end());
        if (files.size() > kMaxEntries)
        {
            return makeError("Too many VPK entries");
        }
        TempOutput    temp {output.string() + ".tmp-" + StableId::generate().toString()};
        std::ofstream packed(temp.path, std::ios::binary | std::ios::trunc);
        if (!packed)
        {
            return makeError("Create VPK: {}", temp.path.string());
        }
        packed.write(kMagic.data(), std::streamsize(kMagic.size()));
        if (auto result = writeNumbers(packed, {{kVersion, 4}, {files.size(), 4}, {0, 8}}); !result)
        {
            return result;
        }

        struct PackedEntry
        {
            std::string path;
            uint64_t    offset;
            uint64_t    size;
            uint64_t    hash;
        };

        std::vector<PackedEntry> entries;
        // Keep package I/O buffers off the small default WebAssembly stack.
        std::vector<char> buffer(65536);
        for (const auto& [name, path] : files)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                return makeError("Read VPK source: {}", path.string());
            }
            std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> hash(XXH3_createState(), &XXH3_freeState);
            if (!hash || XXH3_64bits_reset(hash.get()) == XXH_ERROR)
            {
                return makeError("Initialize VPK checksum");
            }
            const auto start = offset(packed);
            if (!start)
            {
                return std::unexpected(start.error());
            }
            while (input)
            {
                input.read(buffer.data(), std::streamsize(buffer.size()));
                const auto count = input.gcount();
                packed.write(buffer.data(), count);
                if (!packed || XXH3_64bits_update(hash.get(), buffer.data(), size_t(count)) == XXH_ERROR)
                {
                    return makeError("Write VPK entry: {}", name);
                }
            }
            if (!input.eof())
            {
                return makeError("Read VPK source: {}", path.string());
            }
            const auto end = offset(packed);
            if (!end)
            {
                return std::unexpected(end.error());
            }
            entries.push_back({name, *start, *end - *start, XXH3_64bits_digest(hash.get())});
        }

        const auto indexOffset = offset(packed);
        if (!indexOffset)
        {
            return std::unexpected(indexOffset.error());
        }
        for (const auto& entry : entries)
        {
            if (auto result = writeNumbers(packed, {{entry.path.size(), 4}}); !result)
            {
                return result;
            }
            packed.write(entry.path.data(), std::streamsize(entry.path.size()));
            if (auto result = writeNumbers(packed, {{entry.offset, 8}, {entry.size, 8}, {entry.hash, 8}}); !result)
            {
                return result;
            }
        }
        packed.seekp(16);
        if (auto result = writeNumbers(packed, {{*indexOffset, 8}}); !result)
        {
            return result;
        }
        packed.close();
        if (!packed)
        {
            return makeError("Finalize VPK: {}", temp.path.string());
        }
        std::error_code error;
        std::filesystem::rename(temp.path, output, error);
        if (error)
        {
            return makeError("Publish VPK {}: {}", output.string(), error.message());
        }
        temp.path.clear();
        return {};
    }

    std::expected<void, Error> VpkArchive::embedProject(const std::filesystem::path& executable,
                                                        const std::filesystem::path& projectPack,
                                                        const std::filesystem::path& output)
    {
        const auto exists = pathExists(output);
        if (!exists)
        {
            return std::unexpected(exists.error());
        }
        if (*exists)
        {
            return makeError("Executable output already exists: {}", output.string());
        }
        const auto existing = embeddedProject(executable);
        if (!existing)
        {
            return std::unexpected(existing.error());
        }
        if (*existing)
        {
            return makeError("Executable already contains a project VPK");
        }
        const auto archive = VpkArchive::create(projectPack);
        if (!archive)
        {
            return std::unexpected(archive.error());
        }
        const auto manifest = archive->contains("project.vproject");
        if (!manifest)
        {
            return std::unexpected(manifest.error());
        }
        if (!*manifest)
        {
            return makeError("Project VPK has no project manifest");
        }
        if (auto result = archive->read("project.vproject"); !result)
        {
            return std::unexpected(result.error());
        }

        TempOutput      temp {output.string() + ".tmp-" + StableId::generate().toString()};
        std::error_code error;
        const auto      permissions = std::filesystem::status(executable, error).permissions();
        if (error)
        {
            return makeError("Query executable {}: {}", executable.string(), error.message());
        }
        std::filesystem::copy_file(executable, temp.path, error);
        if (error)
        {
            return makeError("Copy executable {}: {}", executable.string(), error.message());
        }
        std::filesystem::permissions(temp.path,
                                     std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add,
                                     error);
        if (error)
        {
            return makeError("Make {} writable: {}", temp.path.string(), error.message());
        }
        std::ifstream input(projectPack, std::ios::binary | std::ios::ate);
        std::ofstream packed(temp.path, std::ios::binary | std::ios::app);
        if (!input || !packed)
        {
            return makeError("Open executable or project VPK for embedding");
        }
        const auto packSize = input.tellg();
        if (packSize < 0)
        {
            return makeError("Query project VPK size");
        }
        input.seekg(0);
        const auto hash = transferAndHash(input, uint64_t(packSize), &packed);
        if (!hash)
        {
            return std::unexpected(hash.error());
        }
        packed.write(kEmbeddedMagic.data(), std::streamsize(kEmbeddedMagic.size()));
        if (auto result = writeNumbers(packed, {{kEmbeddedVersion, 4}, {0, 4}, {uint64_t(packSize), 8}, {*hash, 8}});
            !result)
        {
            return result;
        }
        packed.close();
        if (!packed)
        {
            return makeError("Finalize embedded project executable");
        }
        std::filesystem::permissions(temp.path, permissions, std::filesystem::perm_options::replace, error);
        if (error)
        {
            return makeError("Restore permissions of {}: {}", temp.path.string(), error.message());
        }
        std::filesystem::rename(temp.path, output, error);
        if (error)
        {
            return makeError("Publish executable {}: {}", output.string(), error.message());
        }
        temp.path.clear();
        return {};
    }

    std::expected<std::optional<VpkArchive>, Error> VpkArchive::embeddedProject(const std::filesystem::path& executable)
    {
        std::ifstream input(executable, std::ios::binary | std::ios::ate);
        if (!input)
        {
            return makeError("Open executable for embedded VPK: {}", executable.string());
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
        std::array<uint64_t, 4> footer {};
        for (size_t i = 0; i < footer.size(); ++i)
        {
            const auto value = readNumber(input, i < 2 ? 4 : 8);
            if (!value)
            {
                return std::unexpected(value.error());
            }
            footer[i] = *value;
        }
        const auto [version, reserved, packSize, checksum] = footer;
        if (version != kEmbeddedVersion || reserved != 0 || packSize < kHeaderSize ||
            packSize > uint64_t(fileLength) - kEmbeddedFooterSize)
        {
            return makeError("Invalid embedded project VPK footer");
        }
        const auto packOffset = uint64_t(fileLength) - kEmbeddedFooterSize - packSize;
        input.seekg(std::streamoff(packOffset));
        const auto hash = transferAndHash(input, packSize);
        if (!hash)
        {
            return std::unexpected(hash.error());
        }
        if (*hash != checksum)
        {
            return makeError("Embedded project VPK checksum failed");
        }
        auto path = absolutePath(executable);
        if (!path)
        {
            return std::unexpected(path.error());
        }
        VpkArchive archive(std::move(*path), {});
        if (auto result = archive.load(packOffset, packSize); !result)
        {
            return std::unexpected(result.error());
        }
        const auto manifest = archive.contains("project.vproject");
        if (!manifest)
        {
            return std::unexpected(manifest.error());
        }
        if (!*manifest)
        {
            return makeError("Embedded project VPK has no project manifest");
        }
        return archive;
    }

} // namespace vultra
