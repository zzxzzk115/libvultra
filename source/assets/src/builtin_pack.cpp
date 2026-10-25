#include <vultra/assets/builtin_pack.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        std::string pathText(const std::filesystem::path& path)
        {
            const auto text = path.generic_u8string();
            return {text.begin(), text.end()};
        }

        std::filesystem::path sourceFile(const std::filesystem::path& root, std::string_view path)
        {
            const auto relative = VpkArchive::canonicalPath(path);
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

    } // namespace

    void packBuiltins(const std::filesystem::path& engineRoot, const std::filesystem::path& output)
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
        std::vector<VpkFile> files;
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
            files.emplace_back(VpkArchive::canonicalPath(pathText(relative)), outputFile);
        }
        // Ship upstream attribution with the compiled implementation, without compiler inputs.
        files.emplace_back("external/vrf_fbx_license.txt", sourceFile(engine, "external/vrf_fbx_license.txt"));
        files.emplace_back("external/flip/LICENSE", sourceFile(engine, "external/flip/LICENSE"));
        files.emplace_back("external/flip/README.vultra.md", sourceFile(engine, "external/flip/README.vultra.md"));
        for (const auto* name : {"LICENSE.txt", "README.md"})
        {
            const auto relative = std::string("external/pcg_basic/") + name;
            files.emplace_back(relative, sourceFile(engine, relative));
        }
        for (const auto* name : {"LICENSE", "README.vultra.md"})
        {
            const auto relative = std::string("external/openpbr/") + name;
            files.emplace_back(relative, sourceFile(engine, relative));
        }
        const auto sdk = engine / "build/sdk";
        if (std::filesystem::is_directory(sdk))
        {
            for (const auto& item : std::filesystem::recursive_directory_iterator(sdk))
            {
                if (item.is_regular_file())
                {
                    files.emplace_back("sdk/" + pathText(item.path().lexically_relative(sdk)), item.path());
                }
            }
        }
        VpkArchive::pack(files, output);
    }
} // namespace vultra
