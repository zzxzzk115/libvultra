#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    // Versioned, uncompressed project package. Paths are project-relative UTF-8.
    class VpkArchive
    {
    public:
        explicit VpkArchive(std::filesystem::path file);

        const std::filesystem::path& file() const;

        bool                   contains(std::string_view path) const;
        uint64_t               size(std::string_view path) const;
        std::vector<std::byte> read(std::string_view path) const;
        void                   extractTo(const std::filesystem::path& directory) const;

        static void packProject(const std::filesystem::path& projectFile,
                                const std::filesystem::path& output,
                                const ShaderCompileOptions&  shaders = {});
        static void packBuiltins(const std::filesystem::path& engineRoot, const std::filesystem::path& output);
        static void embedProject(const std::filesystem::path& executable,
                                 const std::filesystem::path& projectPack,
                                 const std::filesystem::path& output);
        static std::optional<VpkArchive> embeddedProject(const std::filesystem::path& executable);

    private:
        struct Entry
        {
            std::string path;
            uint64_t    offset;
            uint64_t    size;
            uint64_t    hash;
        };

        VpkArchive(std::filesystem::path file, uint64_t offset, uint64_t length);
        void                  load(uint64_t offset, uint64_t length);
        const Entry&          entry(std::string_view path) const;
        std::filesystem::path m_File;
        uint64_t              m_Offset = 0;
        std::vector<Entry>    m_Entries;
    };
} // namespace vultra
