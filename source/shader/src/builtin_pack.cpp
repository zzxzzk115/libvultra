#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/shader/builtin_pack.hpp>
#include <vultra/shader/shader_program.hpp>

#include <algorithm>
#include <array>

namespace vultra
{
    namespace
    {
        std::string pathText(const std::filesystem::path& path)
        {
            const auto text = path.generic_u8string();
            return {text.begin(), text.end()};
        }

        std::expected<std::filesystem::path, Error> canonicalPath(const std::filesystem::path& path)
        {
            std::error_code error;
            auto            result = std::filesystem::canonical(path, error);
            if (error)
            {
                return makeError("Resolve {}: {}", path.string(), error.message());
            }
            return result;
        }

        std::expected<VpkFile, Error> sourceFile(const std::filesystem::path& root, std::string_view path)
        {
            const auto relative = VpkArchive::canonicalPath(path);
            if (!relative)
            {
                return std::unexpected(relative.error());
            }
            const auto resolved = canonicalPath(root / *relative);
            if (!resolved)
            {
                return std::unexpected(resolved.error());
            }
            const auto within = resolved->lexically_relative(root);
            if (within.empty() || within.is_absolute())
            {
                return makeError("Project asset leaves its root: {}", *relative);
            }
            for (const auto& part : within)
            {
                if (part == "..")
                {
                    return makeError("Project asset leaves its root: {}", *relative);
                }
            }
            std::error_code status;
            if (!std::filesystem::is_regular_file(*resolved, status))
            {
                return makeError("Project asset is not a file: {}", *relative);
            }
            return VpkFile {*relative, *resolved};
        }

        // Regular files below a directory. A failed traversal is an error; an entry whose status cannot be read,
        // such as a dangling link, is not a regular file.
        std::expected<std::vector<std::filesystem::path>, Error> regularFiles(const std::filesystem::path& directory)
        {
            std::error_code                               error;
            std::vector<std::filesystem::path>            files;
            std::filesystem::recursive_directory_iterator item(directory, error);
            for (; !error && item != std::filesystem::recursive_directory_iterator(); item.increment(error))
            {
                std::error_code status;
                if (item->is_regular_file(status))
                {
                    files.push_back(item->path());
                }
            }
            if (error)
            {
                return makeError("List {}: {}", directory.string(), error.message());
            }
            return files;
        }
    } // namespace

    std::expected<void, Error> packBuiltins(const std::filesystem::path& engineRoot,
                                            const std::filesystem::path& output)
    {
        const auto engine = canonicalPath(engineRoot);
        if (!engine)
        {
            return std::unexpected(engine.error());
        }
        const auto      passes = *engine / "builtin/shaders/passes";
        std::error_code status;
        if (!std::filesystem::is_directory(passes, status))
        {
            return makeError("Engine shader directory is missing: {}", passes.string());
        }

        // The cook directory belongs to this invocation and is removed after the archive is finalized.
        struct CookDirectory
        {
            std::filesystem::path path;

            ~CookDirectory()
            {
                if (!path.empty())
                {
                    std::error_code ignored;
                    std::filesystem::remove_all(path, ignored);
                }
            }
        } cooked;

        std::error_code error;
        const auto      temporary = std::filesystem::temp_directory_path(error);
        if (error)
        {
            return makeError("Query temporary directory: {}", error.message());
        }
        const auto directory = temporary / ("vultra-shaders-" + StableId::generate().toString());
        std::filesystem::create_directory(directory, error);
        if (error)
        {
            return makeError("Create shader cook directory {}: {}", directory.string(), error.message());
        }
        cooked.path = directory;
        const std::array includes {*engine / "builtin/shaders", *engine / "external"};
        auto             passFiles = regularFiles(passes);
        if (!passFiles)
        {
            return std::unexpected(passFiles.error());
        }
        std::vector<std::filesystem::path> sources;
        for (const auto& file : *passFiles)
        {
            if (file.extension() == ".slang")
            {
                sources.push_back(file);
            }
        }
        std::ranges::sort(sources);
        if (sources.empty())
        {
            return makeError("No built-in shader passes to cook");
        }
        std::vector<VpkFile> files;
        for (const auto& source : sources)
        {
            Logger::core().info("Cooking built-in shader {}", source.filename().string());
            const auto program = ShaderProgram::compile(source, {}, includes, source.filename() == "path_trace.slang");
            if (!program)
            {
                return std::unexpected(program.error());
            }
            if (!program->diagnostics.empty())
            {
                Logger::core().warn("{}", program->diagnostics);
            }
            auto relative = source.lexically_relative(*engine);
            relative.replace_extension(".vshaderc");
            const auto outputFile = cooked.path / relative;
            if (auto saved = program->save(outputFile); !saved)
            {
                return saved;
            }
            const auto entry = VpkArchive::canonicalPath(pathText(relative));
            if (!entry)
            {
                return std::unexpected(entry.error());
            }
            files.emplace_back(*entry, outputFile);
        }
        // Ship upstream attribution with the compiled implementation, without compiler inputs.
        std::vector<std::string> attributions {"external/vrf_fbx_license.txt",
                                               "external/flip/LICENSE",
                                               "external/flip/README.vultra.md"};
        for (const auto* name : {"LICENSE.txt", "README.md"})
        {
            attributions.push_back(std::string("external/pcg_basic/") + name);
        }
        for (const auto* name : {"LICENSE", "README.vultra.md"})
        {
            attributions.push_back(std::string("external/openpbr/") + name);
        }
        for (const auto& relative : attributions)
        {
            auto file = sourceFile(*engine, relative);
            if (!file)
            {
                return std::unexpected(file.error());
            }
            files.push_back(std::move(*file));
        }
        const auto sdk = *engine / "build/sdk";
        if (std::filesystem::is_directory(sdk, status))
        {
            const auto sdkFiles = regularFiles(sdk);
            if (!sdkFiles)
            {
                return std::unexpected(sdkFiles.error());
            }
            for (const auto& file : *sdkFiles)
            {
                files.emplace_back("sdk/" + pathText(file.lexically_relative(sdk)), file);
            }
        }
        return VpkArchive::pack(files, output);
    }
} // namespace vultra
