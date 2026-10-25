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
            // Keep package I/O buffers off the small default WebAssembly stack.
            std::vector<char> buffer(65536);
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

    std::string VpkArchive::canonicalPath(std::string_view text)
    {
        const std::u8string utf8(text.begin(), text.end());
        const auto          path = std::filesystem::path(utf8);
        if (text.empty() || text.find(char(0)) != std::string_view::npos || text.find('\\') != std::string_view::npos ||
            path.is_absolute() || path.has_root_name())
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

    VpkArchive::VpkArchive(std::filesystem::path file) :
        m_File(std::filesystem::absolute(file).lexically_normal())
    {
        load(0, std::filesystem::file_size(m_File));
    }

    VpkArchive::VpkArchive(std::filesystem::path file, uint64_t offset, uint64_t length) :
        m_File(std::filesystem::absolute(file).lexically_normal())
    {
        load(offset, length);
    }

    VpkArchive::VpkArchive(std::filesystem::path identity, std::span<const std::byte> bytes) :
        m_File(std::filesystem::absolute(identity).lexically_normal()),
        m_Bytes(bytes)
    {
        if (identity.empty() || bytes.size() < kHeaderSize)
        {
            throw std::invalid_argument("Embedded VPK requires an identity and a complete header");
        }
        load(0, bytes.size());
    }

    void VpkArchive::load(uint64_t offset, uint64_t length)
    {
        std::ifstream input;
        uint64_t      fileLength = m_Bytes.size();
        if (m_Bytes.empty())
        {
            input.open(m_File, std::ios::binary | std::ios::ate);
            if (!input || input.tellg() < 0)
            {
                throw std::runtime_error("Open VPK: " + m_File.string());
            }
            fileLength = uint64_t(input.tellg());
            input.seekg(std::streamoff(offset));
        }
        if (offset > fileLength || length > fileLength - offset || length < kHeaderSize)
        {
            throw std::runtime_error("VPK bounds or header are invalid");
        }
        m_Offset          = offset;
        uint64_t position = 0;
        // One bounded parser for files, executable regions and immutable embedded bytes.
        const auto read = [&](std::span<char> output)
        {
            if (output.size() > length - position)
            {
                throw std::runtime_error("Truncated VPK index");
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
                    throw std::runtime_error("Read VPK index: " + m_File.string());
                }
            }
            position += output.size();
        };
        const auto number = [&](size_t count)
        {
            std::array<char, 8> data {};
            read(std::span(data).first(count));
            uint64_t value = 0;
            for (size_t i = 0; i < count; ++i)
            {
                value |= uint64_t(uint8_t(data[i])) << (8 * i);
            }
            return value;
        };
        std::array<char, 8> magic {};
        read(magic);
        if (magic != kMagic || number(4) != kVersion)
        {
            throw std::runtime_error("Unsupported VPK format or version");
        }
        const auto count       = number(4);
        const auto indexOffset = number(8);
        if (count > kMaxEntries || indexOffset < kHeaderSize || indexOffset > length)
        {
            throw std::runtime_error("Invalid VPK index bounds");
        }
        position = indexOffset;
        if (input.is_open())
        {
            input.seekg(std::streamoff(offset + indexOffset));
        }
        m_Entries.reserve(size_t(count));
        for (uint64_t i = 0; i < count; ++i)
        {
            const auto pathSize = number(4);
            if (pathSize == 0 || pathSize > kMaxPath || pathSize + 24 > length - position)
            {
                throw std::runtime_error("Invalid VPK entry path size");
            }
            std::string path(size_t(pathSize), '\0');
            read(path);
            const auto normalized = VpkArchive::canonicalPath(path);
            Entry      entry {normalized, number(8), number(8), number(8)};
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
        const auto normalized = VpkArchive::canonicalPath(path);
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

    const std::filesystem::path& VpkArchive::file() const
    {
        return m_File;
    }

    bool VpkArchive::contains(std::string_view path) const
    {
        const auto normalized = VpkArchive::canonicalPath(path);
        return std::ranges::any_of(m_Entries,
                                   [&](const Entry& candidate)
                                   {
                                       return candidate.path == normalized;
                                   });
    }

    uint64_t VpkArchive::size(std::string_view path) const
    {
        return entry(path).size;
    }

    std::vector<std::byte> VpkArchive::read(std::string_view path) const
    {
        const auto& selected = entry(path);
        if (selected.size > std::numeric_limits<size_t>::max() ||
            selected.size > uint64_t(std::numeric_limits<std::streamsize>::max()))
        {
            throw std::runtime_error("VPK entry is too large: " + selected.path);
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
                throw std::runtime_error("Open VPK entry: " + selected.path);
            }
            input.seekg(std::streamoff(m_Offset + selected.offset));
            input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!input)
            {
                throw std::runtime_error("Read VPK entry: " + selected.path);
            }
        }
        if (XXH3_64bits(bytes.data(), bytes.size()) != selected.hash)
        {
            throw std::runtime_error("VPK entry checksum failed: " + selected.path);
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

} // namespace vultra
