#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/platform/os/file.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    template<class T>
    T take(std::expected<T, std::string> result)
    {
        require(bool(result), result ? "" : result.error().c_str());
        return std::move(*result);
    }

    template<class T>
    void reject(const std::expected<T, std::string>& result)
    {
        require(!result && !result.error().empty(), "Expected asset boundary rejection with diagnostics");
    }

    void write(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        const auto result = vultra::writeFileAtomicallyExpected(path, bytes);
        require(bool(result), result ? "" : result.error().c_str());
    }

    size_t materializedDirectories()
    {
        size_t count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path()))
        {
            if (entry.path().filename().string().starts_with("vultra-files-"))
            {
                ++count;
            }
        }
        return count;
    }

    struct Directory
    {
        std::filesystem::path path;

        ~Directory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };
} // namespace

int main()
try
{
    using namespace vultra;
    namespace fs = std::filesystem;
    require(take(VpkArchive::canonicalPathExpected("textures/albedo.png")) == "textures/albedo.png",
            "VPK validation changed a canonical path");
    for (const auto* path : {"",
                             "../asset",
                             "/asset",
                             "folder/../asset",
                             "folder\\asset",
                             "./asset",
                             "folder//asset",
                             "folder/",
                             "C:asset"})
    {
        reject(VpkArchive::canonicalPathExpected(path));
    }
    require(bool(VpkArchive::canonicalPathExpected("ui/\xc2\xa9-\xf0\x9f\x8e\xa8.bin")),
            "VPK rejected valid two/four-byte UTF-8");
    for (const auto& text : {std::string("nul\0name", 8),
                             std::string(1, char(0xff)),
                             std::string("\xc0\xaf", 2),
                             std::string("\xed\xa0\x80", 3),
                             std::string("\xf4\x90\x80\x80", 4)})
    {
        reject(VpkArchive::canonicalPathExpected(text));
    }
    const Directory cleanup {fs::temp_directory_path() / ("vpk-memory-" + StableId::generate().toString())};
    const auto      root = cleanup.path;
    fs::create_directory(root);
    fs::create_directory(root / "loose");
    constexpr std::string_view text = "embedded bytes, including UTF-8: \xe4\xbd\xa0\xe5\xa5\xbd";
    const auto                 expected =
        std::vector<std::byte>(std::as_bytes(std::span(text)).begin(), std::as_bytes(std::span(text)).end());
    write(root / "loose/payload", expected);
    write(root / "loose/empty", {});
    write(root / "outside", expected);
    write(root / "manifest", expected);
    auto loose = take(AssetSource::create(root / "loose"));
    require(take(loose->readExpected("payload")) == expected && take(loose->containsExpected("payload")) &&
                !take(loose->containsExpected("absent")) && take(loose->sizeExpected("empty")) == 0 &&
                take(loose->readExpected("empty")).empty(),
            "Filesystem asset source lost bytes or empty-file semantics");
    reject(loose->readExpected("absent"));
    reject(loose->readExpected("../outside"));
    reject(loose->resolveExpected("."));
    reject(loose->readExpected(fs::path(std::string("bad\0path", 8))));
    reject(AssetSource::create(root / "absent"));
    reject(AssetSource::create(root / "outside"));
    reject(AssetSource::create(fs::path(std::string("bad\0path", 8))));
    fs::create_symlink(root / "outside", root / "loose/link");
    reject(loose->resolveExpected("link"));
    reject(loose->readExpected("link"));
    reject(loose->sizeExpected("link"));
    reject(loose->containsExpected("link"));
    bool       observed = false;
    const auto observer = [&](const fs::path&            identity,
                              std::span<const std::byte> consumed) -> std::expected<void, std::string>
    {
        observed = identity == take(loose->resolveExpected("payload")) && std::ranges::equal(consumed, expected);
        return {};
    };
    require(take(readSourceFileExpected("payload", observer, loose.get())) == expected && observed,
            "Expected observer lost the exact consumed bytes or resolved identity");
    const auto refused = readSourceFileExpected(
        "payload",
        [](const auto&, auto) -> std::expected<void, std::string>
        {
            return std::unexpected("observer refused input");
        },
        loose.get());
    reject(refused);
    require(refused.error() == "observer refused input" && take(loose->readExpected("payload")) == expected,
            "Observer rejection was lost or damaged later reads");
    reject(readSourceFileExpected("empty", {}, loose.get()));

    const std::string unicode = "ui/\xe4\xbd\xa0\xe5\xa5\xbd.bin";
    const std::array  files {VpkFile {"ui/data", root / "loose/payload"},
                            VpkFile {"empty", root / "loose/empty"},
                            VpkFile {unicode, root / "loose/payload"},
                            VpkFile {"project.vproject", root / "manifest"}};
    VpkArchive::pack(files, root / "project.vpk");
    const auto bytes = take(readSourceFileExpected(root / "project.vpk"));
    auto       disk  = take(VpkArchive::open(root / "project.vpk"));
    require(take(disk.readExpected("ui/data")) == expected && take(disk.readExpected(unicode)) == expected,
            "File VPK lost a named UTF-8 entry");
    reject(VpkArchive::open(root / "absent.vpk"));
    reject(VpkArchive::open(fs::path(std::string("bad\0path", 8))));
    {
        auto       packed = take(AssetSource::create(disk));
        const auto path   = take(packed->materializeExpected("ui/data"));
        const auto cache  = path.parent_path().parent_path();
        require(take(readSourceFileExpected(path)) == expected &&
                    take(packed->materializeExpected("ui/data")) == path && !fs::exists(cache / "project.vproject"),
                "On-demand materialization extracted unrelated data or changed its identity");
        packed.reset();
        require(!fs::exists(cache), "Materialized files outlived the owning source");
    }
    const auto extracted  = root / "extracted";
    const auto extraction = disk.extractToExpected(extracted);
    require(bool(extraction) && take(readSourceFileExpected(extracted / "ui/data")) == expected,
            "Expected extraction changed entry bytes");
    reject(disk.extractToExpected(extracted));
    require(take(readSourceFileExpected(extracted / "ui/data")) == expected,
            "Rejected extraction overwrote an existing destination");

    write(root / "plain", expected);
    require(!take(VpkArchive::embeddedProjectExpected(root / "plain")), "Plain file unexpectedly contained a project");
    VpkArchive::embedProject(root / "plain", root / "project.vpk", root / "embedded");
    auto embedded = take(VpkArchive::embeddedProjectExpected(root / "embedded"));
    require(embedded && take(embedded->readExpected("ui/data")) == expected,
            "Executable-region VPK lost its exact entry bytes");
    auto damagedExecutable = take(readSourceFileExpected(root / "embedded"));
    damagedExecutable[damagedExecutable.size() - 24] ^= std::byte {1};
    write(root / "bad-footer", damagedExecutable);
    reject(VpkArchive::embeddedProjectExpected(root / "bad-footer"));
    reject(VpkArchive::embeddedProjectExpected(root / "absent-executable"));

    // Corrupt only index metadata, leaving all payload bytes intact.
    const auto indexedName = std::ranges::search(bytes, std::as_bytes(std::span(std::string_view("ui/data"))));
    require(!indexedName.empty(), "Missing named index record for fault injection");
    const auto nameOffset    = size_t(indexedName.begin() - bytes.begin());
    auto       invalidIndex  = bytes;
    invalidIndex[nameOffset] = std::byte {0xff};
    reject(VpkArchive::open(root / "invalid-utf8-index", invalidIndex));
    invalidIndex = bytes;
    for (size_t i = 0; i < 8; ++i)
    {
        invalidIndex[nameOffset + 7 + i] = std::byte {0xff};
    }
    reject(VpkArchive::open(root / "invalid-entry-range", invalidIndex));
    invalidIndex = bytes;
    std::fill(invalidIndex.begin() + 12, invalidIndex.begin() + 16, std::byte {0xff});
    reject(VpkArchive::open(root / "invalid-entry-count", invalidIndex));
    VpkArchive::pack({}, root / "empty-package.vpk");
    auto emptyPackage = take(VpkArchive::open(root / "empty-package.vpk"));
    require(!take(emptyPackage.containsExpected("absent")), "Empty package fabricated an entry");
    reject(emptyPackage.readExpected("absent"));

    auto       damaged = bytes;
    const auto payload = std::ranges::search(damaged, std::as_bytes(std::span(text)));
    require(!payload.empty(), "Missing known payload for checksum fault injection");
    *payload.begin() ^= std::byte {1};
    // This first matching payload may be the manifest; use the entry's checksum through the virtual source.
    write(root / "damaged.vpk", damaged);
    auto badDisk = take(VpkArchive::open(root / "damaged.vpk"));
    reject(badDisk.readExpected("project.vproject"));
    auto       retry       = take(AssetSource::create(badDisk));
    const auto directories = materializedDirectories();
    reject(retry->materializeExpected("project.vproject"));
    require(materializedDirectories() == directories, "Failed first materialization leaked its candidate directory");
    const auto failedExtraction = root / "failed-extraction";
    reject(badDisk.extractToExpected(failedExtraction));
    require(!fs::exists(failedExtraction), "Failed extraction retained a partial output tree");
    write(root / "damaged.vpk", bytes);
    const auto recovered = take(retry->materializeExpected("project.vproject"));
    require(take(readSourceFileExpected(recovered)) == expected,
            "Materialization did not recover after restoring source bytes");
    retry.reset();

    const std::array conflicting {VpkFile {"node", root / "loose/payload"},
                                  VpkFile {"node/child", root / "loose/payload"}};
    VpkArchive::pack(conflicting, root / "conflict.vpk");
    auto       conflict = take(AssetSource::create(take(VpkArchive::open(root / "conflict.vpk"))));
    const auto retained = take(conflict->materializeExpected("node"));
    reject(conflict->materializeExpected("node/child"));
    require(take(readSourceFileExpected(retained)) == expected &&
                take(conflict->materializeExpected("node")) == retained,
            "Failed subsequent materialization damaged an existing cached file");
    conflict.reset();
    loose.reset();
    fs::remove_all(root);

    // Borrowed bytes survive removal of their physical source; reads remain independently owned.
    auto archive = take(VpkArchive::open(root / "virtual.vpk", bytes));
    auto source  = take(AssetSource::create(archive));
    require(take(archive.containsExpected("ui/data")) && !take(archive.containsExpected("missing")) &&
                take(archive.sizeExpected("empty")) == 0 && take(archive.readExpected("empty")).empty() &&
                take(source->readExpected("ui/data")) == expected &&
                take(source->readExpected(take(source->resolveExpected("ui/data")))) == expected &&
                !fs::exists(source->root()),
            "Memory/file VPK parity or virtual namespace failed");
    reject(source->readExpected("../payload"));
    reject(source->readExpected("missing"));
    reject(archive.containsExpected("../payload"));
    reject(readSourceFileExpected("empty", {}, source.get()));
    auto owned = take(source->readExpected("ui/data"));
    owned.front() ^= std::byte {1};
    require(take(source->readExpected("ui/data")) == expected, "Owned read changed immutable package bytes");
    std::array<std::future<bool>, 4> readers;
    for (auto& reader : readers)
    {
        reader = std::async(std::launch::async,
                            [&]
                            {
                                for (uint32_t iteration = 0; iteration < 32; ++iteration)
                                {
                                    const auto result = source->readExpected("ui/data");
                                    if (!result || *result != expected)
                                    {
                                        return false;
                                    }
                                }
                                return true;
                            });
    }
    for (auto& reader : readers)
    {
        require(reader.get(), "Concurrent immutable reads changed package data");
    }
    for (size_t length : {size_t(0), size_t(23), bytes.size() - 1})
        reject(VpkArchive::open(root / "truncated", std::span(bytes).first(length)));
    damaged    = bytes;
    damaged[8] = std::byte {2};
    reject(VpkArchive::open(root / "version", damaged));
    damaged = bytes;
    std::fill(damaged.begin() + 16, damaged.begin() + 24, std::byte {0xff});
    reject(VpkArchive::open(root / "index", damaged));
    reject(VpkArchive::open({}, bytes));
    std::cout << "Expected assets/VPK: filesystem, memory, executable region, UTF-8/path bounds, observer rejection, "
                 "128 concurrent reads, checksum/extraction/materialization failure and recovery passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
