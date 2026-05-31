#include "editor_app/vultra_package.hpp"

#include <vasset/vasset_registry.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace vultra_app
{
    namespace
    {
        constexpr char kMagic[] = "VULTRAPACKAGE 1\n";

        struct PackageEntry
        {
            std::filesystem::path relPath;
            std::string           md5;
            std::vector<uint8_t>  bytes;
        };

        std::string md5Hex(const std::vector<uint8_t>& data);

        std::string lowerString(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        bool isPackageHiddenSource(const std::filesystem::path& rel)
        {
            if (rel.empty())
                return true;
            if (*rel.begin() == "imported")
                return true;
            if (rel.filename().generic_string() == "asset_registry.tsv")
                return true;

            const auto ext = lowerString(rel.extension().generic_string());
            return ext == ".vimport" || ext == ".vmanifest" || ext == ".vpk" || ext == ".log";
        }

        bool safeRelativePath(const std::filesystem::path& rel)
        {
            if (rel.empty() || rel.is_absolute())
                return false;
            for (const auto& part : rel)
            {
                const auto text = part.generic_string();
                if (text.empty() || text == "." || text == "..")
                    return false;
            }
            return true;
        }

        std::string escapePath(const std::filesystem::path& path)
        {
            std::string out;
            for (const char ch : path.generic_string())
            {
                if (ch == '%' || ch == '\n' || ch == '\r')
                {
                    char encoded[4] {};
                    std::snprintf(encoded, sizeof(encoded), "%%%02X", static_cast<unsigned char>(ch));
                    out += encoded;
                }
                else
                {
                    out.push_back(ch);
                }
            }
            return out;
        }

        std::filesystem::path unescapePath(std::string_view text)
        {
            std::string out;
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == '%' && i + 2 < text.size())
                {
                    const auto hex = std::string(text.substr(i + 1, 2));
                    char*      end = nullptr;
                    const auto value = std::strtoul(hex.c_str(), &end, 16);
                    if (end && *end == '\0')
                    {
                        out.push_back(static_cast<char>(value));
                        i += 2;
                        continue;
                    }
                }
                out.push_back(text[i]);
            }
            return std::filesystem::path(out).lexically_normal();
        }

        std::string pathSetKey(const std::filesystem::path& path) { return path.lexically_normal().generic_string(); }

        bool readFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }

        bool readPackageEntries(const std::filesystem::path& packagePath,
                                std::vector<PackageEntry>&   entries,
                                std::string&                 error)
        {
            std::ifstream in(packagePath, std::ios::binary);
            if (!in)
            {
                error = "failed to open package";
                return false;
            }

            std::string magic(sizeof(kMagic) - 1, '\0');
            in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
            if (magic != std::string_view(kMagic, sizeof(kMagic) - 1))
            {
                error = "unsupported .vultrapackage file";
                return false;
            }

            uint64_t count = 0;
            in.read(reinterpret_cast<char*>(&count), sizeof(count));
            if (!in)
            {
                error = "corrupt package header";
                return false;
            }

            entries.clear();
            entries.reserve(static_cast<size_t>(std::min<uint64_t>(count, 65536)));
            for (uint64_t i = 0; i < count; ++i)
            {
                std::string relLine;
                std::string expectedMd5;
                if (!std::getline(in, relLine) || relLine.empty())
                    std::getline(in, relLine);
                if (!std::getline(in, expectedMd5))
                {
                    error = "corrupt package entry";
                    return false;
                }

                uint64_t size = 0;
                in.read(reinterpret_cast<char*>(&size), sizeof(size));
                if (!in)
                {
                    error = "corrupt package entry size";
                    return false;
                }

                std::vector<uint8_t> bytes(static_cast<size_t>(size));
                if (size > 0)
                    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                char trailingNewline = 0;
                in.read(&trailingNewline, 1);
                if (!in || trailingNewline != '\n')
                {
                    error = "corrupt package payload";
                    return false;
                }

                if (md5Hex(bytes) != lowerString(expectedMd5))
                {
                    error = "package checksum mismatch: " + relLine;
                    return false;
                }

                const auto rel = unescapePath(relLine);
                if (!safeRelativePath(rel))
                {
                    error = "unsafe package path: " + relLine;
                    return false;
                }

                entries.push_back(PackageEntry {
                    .relPath = rel,
                    .md5     = lowerString(expectedMd5),
                    .bytes   = std::move(bytes),
                });
            }

            return true;
        }

        bool readTextFile(const std::filesystem::path& path, std::string& text)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }

        std::string jsonUnescapeString(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            bool escaped = false;
            for (const char ch : text)
            {
                if (escaped)
                {
                    out.push_back(ch);
                    escaped = false;
                }
                else if (ch == '\\')
                {
                    escaped = true;
                }
                else
                {
                    out.push_back(ch);
                }
            }
            return out;
        }

        bool isLocalModelDependency(std::string_view uri)
        {
            return !uri.empty() && !uri.starts_with("data:") && !uri.starts_with("http://") &&
                   !uri.starts_with("https://") && !std::filesystem::path(std::string(uri)).is_absolute();
        }

        bool isUuidText(std::string_view text)
        {
            if (text.size() != 32)
                return false;
            return std::ranges::all_of(text, [](const char ch) {
                return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
            });
        }

        void appendIfSourceDependency(const std::filesystem::path& assetRoot,
                                      const std::filesystem::path& sourceFile,
                                      std::vector<std::filesystem::path>& out)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(sourceFile, ec))
                return;
            const auto rel = std::filesystem::relative(sourceFile.lexically_normal(), assetRoot, ec);
            if (ec || !safeRelativePath(rel) || isPackageHiddenSource(rel))
                return;
            out.push_back(sourceFile.lexically_normal());
        }

        void appendGltfDependencies(const std::filesystem::path& assetRoot,
                                    const std::filesystem::path& gltfPath,
                                    std::vector<std::filesystem::path>& out)
        {
            std::string text;
            if (!readTextFile(gltfPath, text))
                return;

            std::string_view view(text);
            size_t           pos = 0;
            while ((pos = view.find("\"uri\"", pos)) != std::string_view::npos)
            {
                pos += 5;
                const auto colon = view.find(':', pos);
                if (colon == std::string_view::npos)
                    break;
                const auto quote = view.find('"', colon + 1);
                if (quote == std::string_view::npos)
                    break;

                size_t end = quote + 1;
                bool   escaped = false;
                while (end < view.size())
                {
                    const char ch = view[end];
                    if (escaped)
                        escaped = false;
                    else if (ch == '\\')
                        escaped = true;
                    else if (ch == '"')
                        break;
                    ++end;
                }
                if (end >= view.size())
                    break;

                const auto uri = jsonUnescapeString(view.substr(quote + 1, end - quote - 1));
                if (isLocalModelDependency(uri))
                    appendIfSourceDependency(assetRoot, (gltfPath.parent_path() / uri).lexically_normal(), out);
                pos = end + 1;
            }
        }

        void appendMtlDependencies(const std::filesystem::path& assetRoot,
                                   const std::filesystem::path& mtlPath,
                                   std::vector<std::filesystem::path>& out)
        {
            std::ifstream in(mtlPath);
            if (!in)
                return;

            std::string line;
            while (std::getline(in, line))
            {
                std::istringstream stream(line);
                std::string        key;
                stream >> key;
                if (!key.starts_with("map_") && key != "bump" && key != "disp" && key != "decal")
                    continue;

                std::string token;
                std::string value;
                while (stream >> token)
                {
                    if (token.starts_with("-"))
                        continue;
                    value = token;
                }
                if (isLocalModelDependency(value))
                    appendIfSourceDependency(assetRoot, (mtlPath.parent_path() / value).lexically_normal(), out);
            }
        }

        void appendObjDependencies(const std::filesystem::path& assetRoot,
                                   const std::filesystem::path& objPath,
                                   std::vector<std::filesystem::path>& out)
        {
            std::ifstream in(objPath);
            if (!in)
                return;

            std::string line;
            while (std::getline(in, line))
            {
                std::istringstream stream(line);
                std::string        key;
                stream >> key;
                if (key != "mtllib")
                    continue;

                std::string mtl;
                stream >> mtl;
                if (!isLocalModelDependency(mtl))
                    continue;

                const auto mtlPath = (objPath.parent_path() / mtl).lexically_normal();
                appendIfSourceDependency(assetRoot, mtlPath, out);
                appendMtlDependencies(assetRoot, mtlPath, out);
            }
        }

        void appendQuotedSceneDependencies(const std::filesystem::path& assetRoot,
                                           const std::filesystem::path& scenePath,
                                           std::string_view             text,
                                           const vasset::VAssetRegistry* registry,
                                           std::vector<std::filesystem::path>& out)
        {
            size_t pos = 0;
            while ((pos = text.find('"', pos)) != std::string_view::npos)
            {
                size_t end     = pos + 1;
                bool   escaped = false;
                while (end < text.size())
                {
                    const char ch = text[end];
                    if (escaped)
                        escaped = false;
                    else if (ch == '\\')
                        escaped = true;
                    else if (ch == '"')
                        break;
                    ++end;
                }
                if (end >= text.size())
                    break;

                const auto value = jsonUnescapeString(text.substr(pos + 1, end - pos - 1));
                if (value.starts_with("res://"))
                    appendIfSourceDependency(assetRoot, assetRoot / std::filesystem::path(value.substr(6)), out);
                else if (registry && isUuidText(value))
                {
                    vbase::UUID uuid {};
                    if (vbase::try_parse_uuid(value.c_str(), uuid))
                    {
                        const auto entry = registry->lookup(uuid);
                        if (entry.type != vasset::VAssetType::eUnknown && !entry.sourcePath.empty())
                            appendIfSourceDependency(assetRoot, assetRoot / std::filesystem::path(entry.sourcePath), out);
                    }
                }

                pos = end + 1;
            }
        }

        void appendSceneDependencies(const std::filesystem::path& assetRoot,
                                     const std::filesystem::path& scenePath,
                                     const vasset::VAssetRegistry* registry,
                                     std::vector<std::filesystem::path>& out)
        {
            std::string text;
            if (!readTextFile(scenePath, text))
                return;
            appendQuotedSceneDependencies(assetRoot, scenePath, text, registry, out);
        }

        void appendDeclaredSourceDependencies(const std::filesystem::path& assetRoot,
                                              const std::filesystem::path& path,
                                              const vasset::VAssetRegistry* registry,
                                              std::vector<std::filesystem::path>& out)
        {
            const auto ext = lowerString(path.extension().generic_string());
            if (ext == ".gltf")
                appendGltfDependencies(assetRoot, path, out);
            else if (ext == ".obj")
                appendObjDependencies(assetRoot, path, out);
            else if (ext == ".mtl")
                appendMtlDependencies(assetRoot, path, out);
            else if (ext == ".vscn")
                appendSceneDependencies(assetRoot, path, registry, out);
        }

        void appendGltfDependenciesFromText(const std::filesystem::path& relPath,
                                            std::string_view             text,
                                            std::vector<std::filesystem::path>& out)
        {
            size_t pos = 0;
            while ((pos = text.find("\"uri\"", pos)) != std::string_view::npos)
            {
                pos += 5;
                const auto colon = text.find(':', pos);
                if (colon == std::string_view::npos)
                    break;
                const auto quote = text.find('"', colon + 1);
                if (quote == std::string_view::npos)
                    break;

                size_t end = quote + 1;
                bool   escaped = false;
                while (end < text.size())
                {
                    const char ch = text[end];
                    if (escaped)
                        escaped = false;
                    else if (ch == '\\')
                        escaped = true;
                    else if (ch == '"')
                        break;
                    ++end;
                }
                if (end >= text.size())
                    break;

                const auto uri = jsonUnescapeString(text.substr(quote + 1, end - quote - 1));
                if (isLocalModelDependency(uri))
                    out.push_back((relPath.parent_path() / uri).lexically_normal());
                pos = end + 1;
            }
        }

        void appendMtlDependenciesFromText(const std::filesystem::path& relPath,
                                           std::string_view             text,
                                           std::vector<std::filesystem::path>& out)
        {
            std::istringstream lines {std::string(text)};
            std::string        line;
            while (std::getline(lines, line))
            {
                std::istringstream stream(line);
                std::string        key;
                stream >> key;
                if (!key.starts_with("map_") && key != "bump" && key != "disp" && key != "decal")
                    continue;

                std::string token;
                std::string value;
                while (stream >> token)
                {
                    if (token.starts_with("-"))
                        continue;
                    value = token;
                }
                if (isLocalModelDependency(value))
                    out.push_back((relPath.parent_path() / value).lexically_normal());
            }
        }

        void appendObjDependenciesFromText(const std::filesystem::path& relPath,
                                           std::string_view             text,
                                           std::vector<std::filesystem::path>& out)
        {
            std::istringstream lines {std::string(text)};
            std::string        line;
            while (std::getline(lines, line))
            {
                std::istringstream stream(line);
                std::string        key;
                stream >> key;
                if (key != "mtllib")
                    continue;

                std::string mtl;
                stream >> mtl;
                if (isLocalModelDependency(mtl))
                    out.push_back((relPath.parent_path() / mtl).lexically_normal());
            }
        }

        void appendSceneDependenciesFromText(const std::filesystem::path& relPath,
                                             std::string_view             text,
                                             std::vector<std::filesystem::path>& out)
        {
            size_t pos = 0;
            while ((pos = text.find('"', pos)) != std::string_view::npos)
            {
                size_t end     = pos + 1;
                bool   escaped = false;
                while (end < text.size())
                {
                    const char ch = text[end];
                    if (escaped)
                        escaped = false;
                    else if (ch == '\\')
                        escaped = true;
                    else if (ch == '"')
                        break;
                    ++end;
                }
                if (end >= text.size())
                    break;

                const auto value = jsonUnescapeString(text.substr(pos + 1, end - pos - 1));
                if (value.starts_with("res://"))
                    out.push_back(std::filesystem::path(value.substr(6)).lexically_normal());
                pos = end + 1;
            }
        }

        std::vector<std::filesystem::path> declaredPackageDependencies(const PackageEntry& entry)
        {
            const auto ext = lowerString(entry.relPath.extension().generic_string());
            if (ext != ".gltf" && ext != ".obj" && ext != ".mtl" && ext != ".vscn")
                return {};

            const std::string text(reinterpret_cast<const char*>(entry.bytes.data()), entry.bytes.size());
            std::vector<std::filesystem::path> deps;
            if (ext == ".gltf")
                appendGltfDependenciesFromText(entry.relPath, text, deps);
            else if (ext == ".obj")
                appendObjDependenciesFromText(entry.relPath, text, deps);
            else if (ext == ".mtl")
                appendMtlDependenciesFromText(entry.relPath, text, deps);
            else if (ext == ".vscn")
                appendSceneDependenciesFromText(entry.relPath, text, deps);
            return deps;
        }

        uint32_t rotl(const uint32_t value, const uint32_t shift) { return (value << shift) | (value >> (32u - shift)); }

        std::array<uint8_t, 16> md5Digest(const std::vector<uint8_t>& data)
        {
            static constexpr std::array<uint32_t, 64> k {
                0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
                0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
                0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
                0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
                0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
                0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
                0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
                0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
                0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
                0xeb86d391,
            };
            static constexpr std::array<uint32_t, 64> s {
                7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22,
                5,  9,  14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 5,  9,  14, 20,
                4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
                6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21,
            };

            std::vector<uint8_t> msg = data;
            const uint64_t       bitLen = static_cast<uint64_t>(msg.size()) * 8u;
            msg.push_back(0x80u);
            while ((msg.size() % 64u) != 56u)
                msg.push_back(0u);
            for (uint32_t i = 0; i < 8; ++i)
                msg.push_back(static_cast<uint8_t>((bitLen >> (8u * i)) & 0xffu));

            uint32_t a0 = 0x67452301u;
            uint32_t b0 = 0xefcdab89u;
            uint32_t c0 = 0x98badcfeu;
            uint32_t d0 = 0x10325476u;

            for (size_t offset = 0; offset < msg.size(); offset += 64)
            {
                uint32_t w[16] {};
                for (uint32_t i = 0; i < 16; ++i)
                {
                    const size_t j = offset + i * 4u;
                    w[i] = static_cast<uint32_t>(msg[j]) | (static_cast<uint32_t>(msg[j + 1]) << 8u) |
                           (static_cast<uint32_t>(msg[j + 2]) << 16u) |
                           (static_cast<uint32_t>(msg[j + 3]) << 24u);
                }

                uint32_t a = a0;
                uint32_t b = b0;
                uint32_t c = c0;
                uint32_t d = d0;
                for (uint32_t i = 0; i < 64; ++i)
                {
                    uint32_t f = 0;
                    uint32_t g = 0;
                    if (i < 16)
                    {
                        f = (b & c) | (~b & d);
                        g = i;
                    }
                    else if (i < 32)
                    {
                        f = (d & b) | (~d & c);
                        g = (5u * i + 1u) % 16u;
                    }
                    else if (i < 48)
                    {
                        f = b ^ c ^ d;
                        g = (3u * i + 5u) % 16u;
                    }
                    else
                    {
                        f = c ^ (b | ~d);
                        g = (7u * i) % 16u;
                    }

                    const uint32_t oldD = d;
                    d                   = c;
                    c                   = b;
                    b += rotl(a + f + k[i] + w[g], s[i]);
                    a = oldD;
                }

                a0 += a;
                b0 += b;
                c0 += c;
                d0 += d;
            }

            std::array<uint8_t, 16> digest {};
            const std::array<uint32_t, 4> words {a0, b0, c0, d0};
            for (size_t i = 0; i < words.size(); ++i)
            {
                digest[i * 4 + 0] = static_cast<uint8_t>(words[i] & 0xffu);
                digest[i * 4 + 1] = static_cast<uint8_t>((words[i] >> 8u) & 0xffu);
                digest[i * 4 + 2] = static_cast<uint8_t>((words[i] >> 16u) & 0xffu);
                digest[i * 4 + 3] = static_cast<uint8_t>((words[i] >> 24u) & 0xffu);
            }
            return digest;
        }

        std::string md5Hex(const std::vector<uint8_t>& data)
        {
            const auto digest = md5Digest(data);
            std::string out(32, '0');
            for (size_t i = 0; i < digest.size(); ++i)
                std::snprintf(out.data() + i * 2, 3, "%02x", static_cast<unsigned>(digest[i]));
            return out;
        }

        void collectExportFiles(const std::filesystem::path& assetRoot,
                                const std::filesystem::path& path,
                                std::vector<std::filesystem::path>& out,
                                const vasset::VAssetRegistry* registry)
        {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
                return;

            const auto rel = std::filesystem::relative(path.lexically_normal(), assetRoot, ec);
            if (ec || !safeRelativePath(rel) || isPackageHiddenSource(rel))
                return;

            if (std::filesystem::is_directory(path, ec))
            {
                std::vector<std::filesystem::path> children;
                for (const auto& entry : std::filesystem::directory_iterator(path, ec))
                    children.push_back(entry.path());
                std::sort(children.begin(), children.end());
                for (const auto& child : children)
                    collectExportFiles(assetRoot, child, out, registry);
                return;
            }

            if (!std::filesystem::is_regular_file(path, ec))
                return;

            const auto normalizedPath = path.lexically_normal();
            if (std::find(out.begin(), out.end(), normalizedPath) != out.end())
                return;

            out.push_back(normalizedPath);
            std::vector<std::filesystem::path> deps;
            appendDeclaredSourceDependencies(assetRoot, normalizedPath, registry, deps);
            for (const auto& dep : deps)
                collectExportFiles(assetRoot, dep.is_absolute() ? dep : assetRoot / dep, out, registry);
        }

        bool writeUint64(std::ofstream& out, const uint64_t value)
        {
            out.write(reinterpret_cast<const char*>(&value), sizeof(value));
            return static_cast<bool>(out);
        }
    } // namespace

    std::filesystem::path ensureVultraPackageExtension(std::filesystem::path path)
    {
        if (lowerString(path.extension().generic_string()) != ".vultrapackage")
            path += ".vultrapackage";
        return path;
    }

    VultraPackageValidationResult validateVultraPackage(const std::filesystem::path& packagePath)
    {
        VultraPackageValidationResult result;
        std::vector<PackageEntry>     entries;
        if (!readPackageEntries(packagePath, entries, result.error))
            return result;

        std::unordered_set<std::string> packagePaths;
        packagePaths.reserve(entries.size());
        for (const auto& entry : entries)
            packagePaths.insert(pathSetKey(entry.relPath));

        for (const auto& entry : entries)
        {
            ++result.filesChecked;
            for (const auto& dep : declaredPackageDependencies(entry))
            {
                if (!safeRelativePath(dep) || isPackageHiddenSource(dep))
                    continue;
                if (!packagePaths.contains(pathSetKey(dep)))
                    result.missingDependencies.push_back(entry.relPath.generic_string() + " -> " + dep.generic_string());
            }
        }

        std::sort(result.missingDependencies.begin(), result.missingDependencies.end());
        result.missingDependencies.erase(
            std::unique(result.missingDependencies.begin(), result.missingDependencies.end()),
            result.missingDependencies.end());
        if (!result.missingDependencies.empty())
        {
            result.error = "package is missing declared source dependencies";
            return result;
        }

        result.ok = true;
        return result;
    }

    VultraPackageExportResult exportVultraPackage(const std::filesystem::path&              assetRoot,
                                                  const std::filesystem::path&              packagePath,
                                                  const std::vector<std::filesystem::path>& sourcePaths,
                                                  const vasset::VAssetRegistry*             registry)
    {
        VultraPackageExportResult result;
        std::vector<std::filesystem::path> files;
        for (const auto& path : sourcePaths)
            collectExportFiles(assetRoot.lexically_normal(), path.lexically_normal(), files, registry);
        std::sort(files.begin(), files.end());
        files.erase(std::unique(files.begin(), files.end()), files.end());
        if (files.empty())
        {
            result.error = "no exportable source files selected";
            return result;
        }

        std::error_code ec;
        if (!packagePath.parent_path().empty())
        {
            std::filesystem::create_directories(packagePath.parent_path(), ec);
            if (ec)
            {
                result.error = ec.message();
                return result;
            }
        }

        std::ofstream out(packagePath, std::ios::binary);
        if (!out)
        {
            result.error = "failed to open package for writing";
            return result;
        }

        out.write(kMagic, sizeof(kMagic) - 1);
        if (!writeUint64(out, static_cast<uint64_t>(files.size())))
        {
            result.error = "failed to write package header";
            return result;
        }

        for (const auto& file : files)
        {
            std::vector<uint8_t> bytes;
            if (!readFileBytes(file, bytes))
            {
                result.error = "failed to read " + file.generic_string();
                return result;
            }

            const auto rel = std::filesystem::relative(file, assetRoot, ec);
            if (ec || !safeRelativePath(rel))
            {
                result.error = "source file is outside asset root: " + file.generic_string();
                return result;
            }

            const auto relText = escapePath(rel);
            const auto md5     = md5Hex(bytes);
            out << relText << '\n' << md5 << '\n';
            if (!writeUint64(out, static_cast<uint64_t>(bytes.size())))
            {
                result.error = "failed to write package entry";
                return result;
            }
            if (!bytes.empty())
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            out << '\n';
            if (!out)
            {
                result.error = "failed to write package data";
                return result;
            }
            ++result.filesWritten;
        }

        result.ok = true;
        return result;
    }

    VultraPackageImportResult importVultraPackage(const std::filesystem::path& assetRoot,
                                                  const std::filesystem::path& packagePath)
    {
        VultraPackageImportResult result;
        std::vector<PackageEntry> entries;
        if (!readPackageEntries(packagePath, entries, result.error))
            return result;

        const auto validation = validateVultraPackage(packagePath);
        if (!validation.ok)
        {
            result.error = validation.error;
            if (!validation.missingDependencies.empty())
                result.error += ": " + validation.missingDependencies.front();
            return result;
        }

        for (const auto& entry : entries)
        {
            const auto dst = (assetRoot / entry.relPath).lexically_normal();
            if (lowerString(entry.relPath.extension().generic_string()) == ".vimport")
            {
                ++result.filesSkipped;
                continue;
            }
            std::vector<uint8_t> existing;
            if (std::filesystem::exists(dst) && readFileBytes(dst, existing) && md5Hex(existing) == entry.md5)
            {
                ++result.filesSkipped;
                continue;
            }

            std::error_code ec;
            std::filesystem::create_directories(dst.parent_path(), ec);
            if (ec)
            {
                result.error = ec.message();
                return result;
            }

            std::ofstream out(dst, std::ios::binary);
            if (!out)
            {
                result.error = "failed to write " + dst.generic_string();
                return result;
            }
            if (!entry.bytes.empty())
                out.write(reinterpret_cast<const char*>(entry.bytes.data()), static_cast<std::streamsize>(entry.bytes.size()));
            if (!out)
            {
                result.error = "failed to write " + dst.generic_string();
                return result;
            }
            ++result.filesWritten;
            result.sourcePaths.push_back(dst);
        }

        std::sort(result.sourcePaths.begin(), result.sourcePaths.end());
        result.sourcePaths.erase(std::unique(result.sourcePaths.begin(), result.sourcePaths.end()),
                                 result.sourcePaths.end());
        result.ok = true;
        return result;
    }
} // namespace vultra_app
