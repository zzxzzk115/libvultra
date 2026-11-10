#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/platform/os/file.hpp>

#include <xxhash.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_set>
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

        std::expected<uint64_t, std::string> readNumberExpected(std::istream& input, int bytes)
        {
            uint64_t value = 0;
            for (int i = 0; i < bytes; ++i)
            {
                const int byte = input.get();
                if (byte == EOF)
                {
                    return std::unexpected("Truncated VPK number");
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

        std::expected<uint64_t, std::string>
        transferAndHashExpected(std::istream& input, uint64_t size, std::ostream* output = nullptr)
        {
            std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> hash(XXH3_createState(), &XXH3_freeState);
            if (!hash || XXH3_64bits_reset(hash.get()) == XXH_ERROR)
            {
                return std::unexpected("Initialize embedded VPK checksum");
            }
            // Keep package I/O buffers off the small default WebAssembly stack.
            std::vector<char> buffer(65536);
            while (size > 0)
            {
                const auto count = std::streamsize(std::min<uint64_t>(size, buffer.size()));
                input.read(buffer.data(), count);
                if (!input)
                {
                    return std::unexpected("Read embedded VPK payload");
                }
                if (output)
                {
                    output->write(buffer.data(), count);
                    if (!*output)
                    {
                        return std::unexpected("Write embedded VPK payload");
                    }
                }
                if (XXH3_64bits_update(hash.get(), buffer.data(), size_t(count)) == XXH_ERROR)
                {
                    return std::unexpected("Hash embedded VPK payload");
                }
                size -= uint64_t(count);
            }
            return XXH3_64bits_digest(hash.get());
        }

        uint64_t transferAndHash(std::istream& input, uint64_t size, std::ostream* output = nullptr)
        {
            auto hash = transferAndHashExpected(input, size, output);
            if (!hash)
            {
                throw std::runtime_error(hash.error()); // Legacy executable writer.
            }
            return *hash;
        }

        bool validUtf8(std::string_view text)
        {
            for (size_t i = 0; i < text.size();)
            {
                const auto first = uint8_t(text[i++]);
                if (first < 0x80)
                {
                    continue;
                }
                uint32_t count = 0;
                if (first >= 0xc2 && first <= 0xdf)
                {
                    count = 1;
                }
                else if (first >= 0xe0 && first <= 0xef)
                {
                    count = 2;
                }
                else if (first >= 0xf0 && first <= 0xf4)
                {
                    count = 3;
                }
                if (!count || count > text.size() - i)
                {
                    return false;
                }
                uint32_t value = first & ((1u << (6 - count)) - 1);
                for (uint32_t j = 0; j < count; ++j)
                {
                    const auto next = uint8_t(text[i++]);
                    if ((next & 0xc0) != 0x80)
                    {
                        return false;
                    }
                    value = (value << 6) | (next & 0x3f);
                }
                if ((count == 1 && value < 0x80) || (count == 2 && value < 0x800) || (count == 3 && value < 0x10000) ||
                    value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
                {
                    return false;
                }
            }
            return true;
        }

        std::expected<std::filesystem::path, std::string> archiveIdentity(const std::filesystem::path& file)
        {
            if (file.empty() ||
                file.native().find(std::filesystem::path::value_type(0)) != std::filesystem::path::string_type::npos)
            {
                return std::unexpected("VPK requires a nonempty identity without NUL characters");
            }
            std::error_code error;
            auto            absolute = std::filesystem::absolute(file, error);
            if (error)
            {
                return std::unexpected("Resolve VPK identity: " + error.message());
            }
            return absolute.lexically_normal();
        }

        // A bounded region of either a file or borrowed immutable package bytes.
        struct ArchiveInput
        {
            std::ifstream              file;
            std::span<const std::byte> bytes;
            uint64_t                   offset   = 0;
            uint64_t                   length   = 0;
            uint64_t                   position = 0;

            static std::expected<ArchiveInput, std::string>
            open(const std::filesystem::path& path, std::span<const std::byte> bytes, uint64_t offset, uint64_t length)
            {
                ArchiveInput input;
                input.bytes         = bytes;
                input.offset        = offset;
                input.length        = length;
                uint64_t fileLength = bytes.size();
                if (bytes.empty())
                {
                    input.file.open(path, std::ios::binary | std::ios::ate);
                    const auto end = input.file ? input.file.tellg() : std::streampos(-1);
                    if (end < std::streamoff(0))
                    {
                        return std::unexpected("Open VPK: " + path.string());
                    }
                    fileLength = uint64_t(end);
                }
                if (offset > fileLength || length > fileLength - offset)
                {
                    return std::unexpected("VPK region leaves its source");
                }
                if (auto ready = input.seek(0); !ready)
                {
                    return std::unexpected(ready.error());
                }
                return input;
            }

            std::expected<void, std::string> seek(uint64_t next)
            {
                if (next > length)
                {
                    return std::unexpected("VPK seek leaves its region");
                }
                if (file.is_open())
                {
                    file.seekg(std::streamoff(offset + next));
                    if (!file)
                    {
                        return std::unexpected("Seek VPK region");
                    }
                }
                position = next;
                return {};
            }

            std::expected<void, std::string> read(std::span<char> output)
            {
                if (output.size() > length - position)
                {
                    return std::unexpected("Truncated VPK region");
                }
                if (output.empty())
                {
                    return {};
                }
                if (!bytes.empty())
                {
                    std::memcpy(output.data(), bytes.data() + size_t(offset + position), output.size());
                }
                else
                {
                    file.read(output.data(), std::streamsize(output.size()));
                    if (!file)
                    {
                        return std::unexpected("Read VPK region");
                    }
                }
                position += output.size();
                return {};
            }
        };

        uint64_t littleEndian(std::span<const char> bytes)
        {
            uint64_t value = 0;
            for (size_t i = 0; i < bytes.size(); ++i)
            {
                value |= uint64_t(uint8_t(bytes[i])) << (8 * i);
            }
            return value;
        }

    } // namespace

    std::expected<std::string, std::string> VpkArchive::canonicalPathExpected(std::string_view text)
    {
        if (text.empty() || text.size() > kMaxPath || text.front() == '/' ||
            text.find(char(0)) != std::string_view::npos || text.find('\\') != std::string_view::npos ||
            !validUtf8(text) ||
            (text.size() >= 2 && text[1] == ':' &&
             ((text[0] >= 'a' && text[0] <= 'z') || (text[0] >= 'A' && text[0] <= 'Z'))))
        {
            return std::unexpected("VPK requires a canonical relative UTF-8 file name");
        }
        size_t start = 0;
        while (start <= text.size())
        {
            const auto end  = text.find('/', start);
            const auto part = text.substr(start, end == std::string_view::npos ? end : end - start);
            if (part.empty() || part == "." || part == "..")
            {
                return std::unexpected("VPK path is not canonical");
            }
            if (end == std::string_view::npos)
            {
                break;
            }
            start = end + 1;
        }
        return std::string(text);
    }

    std::string VpkArchive::canonicalPath(std::string_view path)
    {
        auto canonical = canonicalPathExpected(path);
        if (!canonical)
        {
            throw std::invalid_argument(canonical.error());
        }
        return std::move(*canonical);
    }

    std::expected<VpkArchive, std::string> VpkArchive::open(const std::filesystem::path& file)
    {
        auto identity = archiveIdentity(file);
        if (!identity)
        {
            return std::unexpected(identity.error());
        }
        std::error_code error;
        const auto      size = std::filesystem::file_size(*identity, error);
        if (error)
        {
            return std::unexpected("Query VPK size: " + error.message());
        }
        VpkArchive archive(Uninitialized {});
        archive.m_File = std::move(*identity);
        if (auto loaded = archive.loadExpected(0, size); !loaded)
        {
            return std::unexpected(loaded.error());
        }
        return archive;
    }

    std::expected<VpkArchive, std::string> VpkArchive::open(const std::filesystem::path& identity,
                                                            std::span<const std::byte>   bytes)
    {
        auto path = archiveIdentity(identity);
        if (!path)
        {
            return std::unexpected(path.error());
        }
        if (bytes.size() < kHeaderSize)
        {
            return std::unexpected("Embedded VPK requires a complete header");
        }
        VpkArchive archive(Uninitialized {});
        archive.m_File  = std::move(*path);
        archive.m_Bytes = bytes;
        if (auto loaded = archive.loadExpected(0, bytes.size()); !loaded)
        {
            return std::unexpected(loaded.error());
        }
        return archive;
    }

    VpkArchive::VpkArchive(std::filesystem::path file) :
        VpkArchive(Uninitialized {})
    {
        auto archive = open(file);
        if (!archive)
        {
            throw std::runtime_error(archive.error());
        }
        *this = std::move(*archive);
    }

    VpkArchive::VpkArchive(std::filesystem::path identity, std::span<const std::byte> bytes) :
        VpkArchive(Uninitialized {})
    {
        auto archive = open(identity, bytes);
        if (!archive)
        {
            throw std::runtime_error(archive.error());
        }
        *this = std::move(*archive);
    }

    std::expected<void, std::string> VpkArchive::loadExpected(uint64_t offset, uint64_t length)
    {
        auto input = ArchiveInput::open(m_File, m_Bytes, offset, length);
        if (!input)
        {
            return std::unexpected(input.error());
        }
        std::array<char, kHeaderSize> header {};
        if (auto read = input->read(header); !read)
        {
            return read;
        }
        const auto data = std::span(header);
        if (!std::equal(kMagic.begin(), kMagic.end(), header.begin()) || littleEndian(data.subspan(8, 4)) != kVersion)
        {
            return std::unexpected("Unsupported VPK format or version");
        }
        const auto count       = littleEndian(data.subspan(12, 4));
        const auto indexOffset = littleEndian(data.subspan(16, 8));
        if (count > kMaxEntries || indexOffset < kHeaderSize || indexOffset > length ||
            count > (length - indexOffset) / 29)
        {
            return std::unexpected("Invalid VPK index bounds");
        }
        if (auto seek = input->seek(indexOffset); !seek)
        {
            return seek;
        }
        std::vector<Entry> entries;
        entries.reserve(size_t(count));
        // Reserve the complete entry array before borrowing its names for duplicate detection.
        std::unordered_set<std::string_view> names;
        for (uint64_t i = 0; i < count; ++i)
        {
            std::array<char, 4> pathLength {};
            if (auto read = input->read(pathLength); !read)
            {
                return read;
            }
            const auto pathSize = littleEndian(pathLength);
            if (!pathSize || pathSize > kMaxPath || pathSize + 24 > length - input->position)
            {
                return std::unexpected("Invalid VPK entry path size");
            }
            std::string name(size_t(pathSize), char(0));
            if (auto read = input->read(name); !read)
            {
                return read;
            }
            auto normalized = canonicalPathExpected(name);
            if (!normalized)
            {
                return std::unexpected(normalized.error());
            }
            std::array<char, 24> fields {};
            if (auto read = input->read(fields); !read)
            {
                return read;
            }
            const auto values = std::span(fields);
            Entry      entry {std::move(*normalized),
                         littleEndian(values.first(8)),
                         littleEndian(values.subspan(8, 8)),
                         littleEndian(values.subspan(16, 8))};
            if (entry.offset < kHeaderSize || entry.offset > indexOffset || entry.size > indexOffset - entry.offset ||
                names.contains(entry.path))
            {
                return std::unexpected("Invalid or duplicate VPK entry: " + entry.path);
            }
            entries.push_back(std::move(entry));
            names.insert(entries.back().path);
        }
        m_Offset  = offset;
        m_Entries = std::move(entries);
        return {};
    }

    std::expected<const VpkArchive::Entry*, std::string> VpkArchive::entryExpected(std::string_view path) const
    {
        auto normalized = canonicalPathExpected(path);
        if (!normalized)
        {
            return std::unexpected(normalized.error());
        }
        const auto found = std::ranges::find(m_Entries, *normalized, &Entry::path);
        if (found == m_Entries.end())
            return std::unexpected("VPK entry not found: " + *normalized);
        return &*found;
    }

    const std::filesystem::path& VpkArchive::file() const
    {
        return m_File;
    }

    std::expected<bool, std::string> VpkArchive::containsExpected(std::string_view path) const
    {
        auto normalized = canonicalPathExpected(path);
        if (!normalized)
        {
            return std::unexpected(normalized.error());
        }
        return std::ranges::find(m_Entries, *normalized, &Entry::path) != m_Entries.end();
    }

    std::expected<uint64_t, std::string> VpkArchive::sizeExpected(std::string_view path) const
    {
        auto selected = entryExpected(path);
        if (!selected)
        {
            return std::unexpected(selected.error());
        }
        return (*selected)->size;
    }

    std::expected<std::vector<std::byte>, std::string> VpkArchive::readExpected(std::string_view path) const
    {
        auto selected = entryExpected(path);
        if (!selected)
        {
            return std::unexpected(selected.error());
        }
        const auto& entry = **selected;
        if (entry.size > std::numeric_limits<size_t>::max() ||
            entry.size > uint64_t(std::numeric_limits<std::streamsize>::max()))
        {
            return std::unexpected("VPK entry is too large: " + entry.path);
        }
        std::vector<std::byte> bytes;
        if (entry.size > bytes.max_size())
        {
            return std::unexpected("VPK entry exceeds container size: " + entry.path);
        }
        bytes.resize(size_t(entry.size));
        auto input = ArchiveInput::open(m_File, m_Bytes, m_Offset + entry.offset, entry.size);
        if (!input)
        {
            return std::unexpected(input.error());
        }
        if (auto read = input->read({reinterpret_cast<char*>(bytes.data()), bytes.size()}); !read)
        {
            return std::unexpected(read.error());
        }
        if (XXH3_64bits(bytes.data(), bytes.size()) != entry.hash)
        {
            return std::unexpected("VPK entry checksum failed: " + entry.path);
        }
        return bytes;
    }

    bool VpkArchive::contains(std::string_view path) const
    {
        auto result = containsExpected(path);
        if (!result)
        {
            throw std::invalid_argument(result.error());
        }
        return *result;
    }

    uint64_t VpkArchive::size(std::string_view path) const
    {
        auto result = sizeExpected(path);
        if (!result)
        {
            throw std::invalid_argument(result.error());
        }
        return *result;
    }

    std::vector<std::byte> VpkArchive::read(std::string_view path) const
    {
        auto result = readExpected(path);
        if (!result)
        {
            throw std::runtime_error(result.error());
        }
        return std::move(*result);
    }

    std::expected<void, std::string> VpkArchive::extractToExpected(const std::filesystem::path& directory) const
    {
        std::error_code error;
        const bool      created = std::filesystem::create_directories(directory, error);
        if (error || !created)
        {
            return std::unexpected("VPK extraction requires a fresh directory: " + error.message());
        }
        const auto fail = [&](std::string reason) -> std::expected<void, std::string>
        {
            std::error_code cleanup;
            std::filesystem::remove_all(directory, cleanup);
            if (cleanup)
            {
                reason += "; remove extraction directory: " + cleanup.message();
            }
            return std::unexpected(std::move(reason));
        };
        for (const auto& selected : m_Entries)
        {
            const auto path =
                directory / std::filesystem::path(std::u8string(selected.path.begin(), selected.path.end()));
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
            {
                return fail("Create VPK extraction directory: " + error.message());
            }
            auto bytes = readExpected(selected.path);
            if (!bytes)
            {
                return fail(bytes.error());
            }
            if (auto written = writeFileAtomicallyExpected(path, *bytes); !written)
            {
                return fail(written.error());
            }
        }
        return {};
    }

    void VpkArchive::extractTo(const std::filesystem::path& directory) const
    {
        if (auto result = extractToExpected(directory); !result)
        {
            throw std::runtime_error(result.error());
        }
    }

    void VpkArchive::pack(std::span<const VpkFile> inputFiles, const std::filesystem::path& output)
    {
        std::vector<VpkFile> files(inputFiles.begin(), inputFiles.end());
        for (auto& file : files)
        {
            file.path = VpkArchive::canonicalPath(file.path);
        }
        if (std::filesystem::exists(output))
        {
            throw std::invalid_argument("VPK output already exists: " + output.string());
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
                throw std::invalid_argument("Conflicting VPK entry path: " + files[i].path);
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
        // Keep package I/O buffers off the small default WebAssembly stack.
        std::vector<char> buffer(65536);
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

    std::expected<std::optional<VpkArchive>, std::string>
    VpkArchive::embeddedProjectExpected(const std::filesystem::path& executable)
    {
        auto identity = archiveIdentity(executable);
        if (!identity)
        {
            return std::unexpected(identity.error());
        }
        std::ifstream input(*identity, std::ios::binary | std::ios::ate);
        const auto    fileLength = input ? input.tellg() : std::streampos(-1);
        if (fileLength < std::streamoff(0))
        {
            return std::unexpected("Open executable for embedded VPK: " + identity->string());
        }
        if (fileLength < std::streamoff(kEmbeddedFooterSize))
            return std::optional<VpkArchive> {};
        input.seekg(fileLength - std::streamoff(kEmbeddedFooterSize));
        std::array<char, 8> magic {};
        input.read(magic.data(), std::streamsize(magic.size()));
        if (!input)
        {
            return std::unexpected("Read embedded VPK footer");
        }
        if (magic != kEmbeddedMagic)
            return std::optional<VpkArchive> {};
        std::array<uint64_t, 4> fields {};
        for (size_t i = 0; i < fields.size(); ++i)
        {
            auto value = readNumberExpected(input, i < 2 ? 4 : 8);
            if (!value)
            {
                return std::unexpected(value.error());
            }
            fields[i] = *value;
        }
        const auto [version, reserved, packSize, expected] = fields;
        if (version != kEmbeddedVersion || reserved != 0 || packSize < kHeaderSize ||
            packSize > uint64_t(fileLength) - kEmbeddedFooterSize)
            return std::unexpected("Invalid embedded project VPK footer");
        const auto offset = uint64_t(fileLength) - kEmbeddedFooterSize - packSize;
        input.seekg(std::streamoff(offset));
        auto hash = transferAndHashExpected(input, packSize);
        if (!hash)
        {
            return std::unexpected(hash.error());
        }
        if (*hash != expected)
        {
            return std::unexpected("Embedded project VPK checksum failed");
        }
        VpkArchive archive(Uninitialized {});
        archive.m_File = std::move(*identity);
        if (auto loaded = archive.loadExpected(offset, packSize); !loaded)
        {
            return std::unexpected(loaded.error());
        }
        auto manifest = archive.containsExpected("project.vproject");
        if (!manifest)
        {
            return std::unexpected(manifest.error());
        }
        if (!*manifest)
        {
            return std::unexpected("Embedded project VPK has no project manifest");
        }
        return std::optional<VpkArchive> {std::move(archive)};
    }

    std::optional<VpkArchive> VpkArchive::embeddedProject(const std::filesystem::path& executable)
    {
        auto archive = embeddedProjectExpected(executable);
        if (!archive)
        {
            throw std::runtime_error(archive.error());
        }
        return std::move(*archive);
    }
} // namespace vultra
