#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace vultra
{
    namespace
    {
        constexpr int kProjectVersion = 1;

        std::string pathText(const std::filesystem::path& path)
        {
            const auto text = path.generic_u8string();
            return {text.begin(), text.end()};
        }

        std::filesystem::path projectPath(std::string_view text)
        {
            const std::u8string utf8(text.begin(), text.end());
            const auto          input = std::filesystem::path(utf8);
            if (input.empty() || input.is_absolute() || input.has_root_name())
            {
                throw std::invalid_argument("Project path must be relative");
            }
            for (const auto& part : input)
            {
                if (part == "..")
                {
                    throw std::invalid_argument("Project path may not leave its directory");
                }
            }
            const auto path = input.lexically_normal();
            if (path == ".")
            {
                throw std::invalid_argument("Project path must name a file");
            }
            return path;
        }

        const char* languageName(ScriptModule::Language language)
        {
            switch (language)
            {
                case ScriptModule::Language::eNative:
                    return "native";
                case ScriptModule::Language::eLua:
                    return "lua";
                case ScriptModule::Language::eCSharp:
                    return "csharp";
            }
            throw std::invalid_argument("Unknown script language");
        }

        ScriptModule::Language parseLanguage(std::string_view name)
        {
            if (name == "native")
            {
                return ScriptModule::Language::eNative;
            }
            if (name == "lua")
            {
                return ScriptModule::Language::eLua;
            }
            if (name == "csharp")
            {
                return ScriptModule::Language::eCSharp;
            }
            throw std::invalid_argument("Unknown script language");
        }

        AssetId parseAssetId(const std::string& text)
        {
            const auto parsed = StableId::parse(text);
            if (!parsed)
            {
                throw std::invalid_argument("Invalid asset ID: " + text);
            }
            return {*parsed};
        }
    } // namespace

    AssetId ProjectManifest::addAsset(const std::filesystem::path& path, std::optional<FbxImportOptions> fbx)
    {
        const auto normalized = projectPath(pathText(path));
        if (std::ranges::any_of(m_Assets,
                                [&](const ProjectAsset& asset)
                                {
                                    return asset.path == normalized;
                                }))
        {
            throw std::invalid_argument("Duplicate project asset path: " + pathText(normalized));
        }
        AssetId id;
        do
        {
            id.value = StableId::generate();
        } while (std::ranges::any_of(m_Assets,
                                     [&](const ProjectAsset& asset)
                                     {
                                         return asset.id == id;
                                     }));
        m_Assets.push_back({id, normalized, fbx});
        return id;
    }

    void ProjectManifest::renameAsset(AssetId id, const std::filesystem::path& path)
    {
        const auto normalized = projectPath(pathText(path));
        if (std::ranges::any_of(m_Assets,
                                [&](const ProjectAsset& asset)
                                {
                                    return asset.path == normalized && asset.id != id;
                                }))
        {
            throw std::invalid_argument("Duplicate project asset path: " + pathText(normalized));
        }
        const auto entry = std::ranges::find_if(m_Assets,
                                                [&](const ProjectAsset& candidate)
                                                {
                                                    return candidate.id == id;
                                                });
        if (entry == m_Assets.end())
        {
            throw std::invalid_argument("Unknown project asset ID: " + id.value.toString());
        }
        entry->path = normalized;
    }

    const ProjectAsset& ProjectManifest::asset(AssetId id) const
    {
        const auto entry = std::ranges::find_if(m_Assets,
                                                [&](const ProjectAsset& candidate)
                                                {
                                                    return candidate.id == id;
                                                });
        if (entry == m_Assets.end())
        {
            throw std::invalid_argument("Unknown project asset ID: " + id.value.toString());
        }
        return *entry;
    }

    const std::vector<ProjectAsset>& ProjectManifest::assets() const
    {
        return m_Assets;
    }

    void ProjectManifest::validate() const
    {
        projectPath(pathText(mainScene));
        for (size_t i = 0; i < m_Assets.size(); ++i)
        {
            const auto& entry = m_Assets[i];
            if (!entry.id.value.valid())
            {
                throw std::invalid_argument("Project asset ID is empty");
            }
            projectPath(pathText(entry.path));
            auto extension = entry.path.extension().string();
            std::ranges::transform(extension,
                                   extension.begin(),
                                   [](unsigned char c)
                                   {
                                       return char(std::tolower(c));
                                   });
            if (entry.fbx && ((extension != ".fbx") ||
                              (entry.fbx->materialConvention != FbxMaterialConvention::ePhong &&
                               entry.fbx->materialConvention != FbxMaterialConvention::eOrcaMetallicRoughness)))
            {
                throw std::invalid_argument("fbx_import requires an FBX asset and a supported material convention");
            }
            for (size_t j = 0; j < i; ++j)
            {
                if (m_Assets[j].id == entry.id || m_Assets[j].path == entry.path)
                {
                    throw std::invalid_argument("Duplicate project asset ID or path");
                }
            }
        }
        if (environment)
        {
            asset(*environment);
        }
        if (uiDocument)
        {
            asset(*uiDocument);
        }
        if (uiFont)
        {
            asset(*uiFont);
        }
        if (research)
        {
            if (research->name.empty() || research->name.find_first_of("/\\:") != std::string::npos ||
                research->name == "." || research->name == ".." || research->methods.empty() || research->width < 11 ||
                research->height < 11 || research->width > 8192 || research->height > 8192)
            {
                throw std::invalid_argument("Research project requires a name, methods and valid eye extent");
            }
            if (research->renderPath != "forward" && research->renderPath != "deferred")
            {
                throw std::invalid_argument("Research render_path must be forward or deferred");
            }
            const auto renderer = nlohmann::json::parse(research->rendererSettings);
            if (!renderer.is_object() || renderer.contains("path"))
            {
                throw std::invalid_argument("Research renderer must be an object; use render_path to select the path");
            }
            asset(research->comparison);
            if (research->configurationLabel.empty() ||
                (!research->referenceMethod.empty() &&
                 (research->methods.size() < 2 ||
                  std::ranges::find(research->methods, research->referenceMethod, &ResearchMethod::name) ==
                      research->methods.end())))
            {
                throw std::invalid_argument(
                    "Research reference method must name an existing method with an alternative");
            }
            for (size_t i = 0; i < research->methods.size(); ++i)
            {
                const auto& method = research->methods[i];
                asset(method.graph);
                if (method.name.empty())
                {
                    throw std::invalid_argument("Research method name is empty");
                }
                for (size_t j = 0; j < i; ++j)
                {
                    if (research->methods[j].name == method.name)
                    {
                        throw std::invalid_argument("Duplicate research method name");
                    }
                }
            }
        }
        for (size_t index = 0; index < extensions.size(); ++index)
        {
            projectPath(pathText(extensions[index]));
            if (std::ranges::find(extensions.begin(), extensions.begin() + index, extensions[index]) !=
                extensions.begin() + index)
            {
                throw std::invalid_argument("Duplicate project extension path");
            }
        }
        for (const auto& script : scripts)
        {
            projectPath(pathText(script.path));
            languageName(script.language);
            if (script.node && !script.node->valid())
            {
                throw std::invalid_argument("Script target node ID is empty");
            }
            if (script.language == ScriptModule::Language::eCSharp && (script.typeName.empty() || !script.node))
            {
                throw std::invalid_argument("C# script requires a class name and scene node ID");
            }
        }
    }

    void ProjectManifest::save(const std::filesystem::path& file) const
    {
        validate();
        nlohmann::json document = {{"format", "vultra.project"},
                                   {"version", kProjectVersion},
                                   {"main_scene", pathText(mainScene)},
                                   {"assets", nlohmann::json::array()},
                                   {"extensions", nlohmann::json::array()},
                                   {"scripts", nlohmann::json::array()}};
        if (environment)
        {
            document["environment"] = environment->value.toString();
        }
        if (uiDocument)
        {
            document["ui_document"] = uiDocument->value.toString();
        }
        if (uiFont)
        {
            document["ui_font"] = uiFont->value.toString();
        }
        for (const auto& asset : m_Assets)
        {
            nlohmann::json entry {{"id", asset.id.value.toString()}, {"path", pathText(asset.path)}};
            if (asset.fbx)
            {
                entry["fbx_import"] = {
                    {"materials", asset.fbx->materialConvention == FbxMaterialConvention::ePhong ? "phong" : "orca"},
                    {"normal_maps", asset.fbx->directXNormalMaps ? "directx" : "opengl"}};
            }
            document["assets"].push_back(std::move(entry));
        }
        for (const auto& extension : extensions)
        {
            document["extensions"].push_back(pathText(extension));
        }
        for (const auto& script : scripts)
        {
            nlohmann::json entry = {{"language", languageName(script.language)}, {"path", pathText(script.path)}};
            if (!script.typeName.empty())
            {
                entry["type"] = script.typeName;
            }
            if (script.node)
            {
                entry["node"] = script.node->toString();
            }
            document["scripts"].push_back(std::move(entry));
        }
        if (research)
        {
            auto& description = document["research"];
            description       = {{"name", research->name},
                                 {"size", {research->width, research->height}},
                                 {"features", research->features},
                                 {"render_path", research->renderPath},
                                 {"reference_method", research->referenceMethod},
                                 {"configuration_label", research->configurationLabel},
                                 {"renderer", nlohmann::json::parse(research->rendererSettings)},
                                 {"comparison", research->comparison.value.toString()},
                                 {"methods", nlohmann::json::array()}};
            for (const auto& method : research->methods)
            {
                description["methods"].push_back({{"name", method.name}, {"graph", method.graph.value.toString()}});
            }
        }
        const auto text = document.dump(2) + "\n";
        writeFileAtomically(file, std::as_bytes(std::span(text)));
    }

    ProjectManifest ProjectManifest::load(const std::filesystem::path& file, const AssetSource* source)
    {
        try
        {
            const auto  bytes    = readSourceFile(file, {}, source);
            const auto* begin    = reinterpret_cast<const char*>(bytes.data());
            const auto  document = nlohmann::json::parse(begin, begin + bytes.size());
            if (document.at("format") != "vultra.project" || document.at("version") != kProjectVersion)
            {
                throw std::invalid_argument("Unsupported project manifest format or version");
            }
            ProjectManifest project;
            project.mainScene = projectPath(document.at("main_scene").get<std::string>());
            if (document.contains("environment"))
            {
                project.environment = parseAssetId(document.at("environment").get<std::string>());
            }
            if (document.contains("ui_document"))
            {
                project.uiDocument = parseAssetId(document.at("ui_document").get<std::string>());
            }
            if (document.contains("ui_font"))
            {
                project.uiFont = parseAssetId(document.at("ui_font").get<std::string>());
            }
            for (const auto& entry : document.at("assets"))
            {
                project.m_Assets.push_back({parseAssetId(entry.at("id").get<std::string>()),
                                            projectPath(entry.at("path").get<std::string>())});
                if (entry.contains("fbx_import"))
                {
                    const auto& description = entry.at("fbx_import");
                    if (!description.is_object())
                    {
                        throw std::invalid_argument("fbx_import must be an object");
                    }
                    for (const auto& setting : description.items())
                    {
                        const auto& key = setting.key();
                        if (key != "materials" && key != "normal_maps")
                        {
                            throw std::invalid_argument("Unknown fbx_import setting: " + key);
                        }
                    }
                    const auto materials = description.value("materials", std::string("phong"));
                    const auto normals   = description.value("normal_maps", std::string("opengl"));
                    if ((materials != "phong" && materials != "orca") || (normals != "opengl" && normals != "directx"))
                    {
                        throw std::invalid_argument("Unknown fbx_import material or normal-map convention");
                    }
                    project.m_Assets.back().fbx =
                        FbxImportOptions {materials == "orca" ? FbxMaterialConvention::eOrcaMetallicRoughness :
                                                                FbxMaterialConvention::ePhong,
                                          normals == "directx"};
                }
            }
            for (const auto& extension : document.at("extensions"))
            {
                project.extensions.push_back(projectPath(extension.get<std::string>()));
            }
            for (const auto& entry : document.at("scripts"))
            {
                ScriptModule script {parseLanguage(entry.at("language").get<std::string>()),
                                     projectPath(entry.at("path").get<std::string>())};
                if (entry.contains("type"))
                {
                    script.typeName = entry.at("type").get<std::string>();
                }
                if (entry.contains("node"))
                {
                    const auto node = StableId::parse(entry.at("node").get<std::string>());
                    if (!node)
                    {
                        throw std::invalid_argument("Invalid script node ID");
                    }
                    script.node = *node;
                }
                project.scripts.push_back(std::move(script));
            }
            if (document.contains("research"))
            {
                const auto&     description = document.at("research");
                ResearchProject research;
                research.name               = description.at("name").get<std::string>();
                research.width              = description.at("size").at(0).get<uint32_t>();
                research.height             = description.at("size").at(1).get<uint32_t>();
                research.features           = description.value("features", uint64_t(0));
                research.renderPath         = description.value("render_path", std::string("forward"));
                research.referenceMethod    = description.value("reference_method", std::string {});
                research.configurationLabel = description.value("configuration_label", std::string("Configuration"));
                research.rendererSettings   = description.value("renderer", nlohmann::json::object()).dump();
                research.comparison         = parseAssetId(description.at("comparison").get<std::string>());
                for (const auto& method : description.at("methods"))
                {
                    research.methods.push_back(
                        {method.at("name").get<std::string>(), parseAssetId(method.at("graph").get<std::string>())});
                }
                project.research = std::move(research);
            }
            project.validate();
            return project;
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("Read project manifest " + file.string() + ": " + error.what());
        }
    }

    std::filesystem::path ProjectManifest::materializeModule(const AssetSource&           source,
                                                             const std::filesystem::path& module) const
    {
        const auto result = source.materialize(module);
        // The .NET host requires adjacent assemblies and its runtime/dependency descriptions.
        if (std::ranges::any_of(scripts,
                                [&](const auto& script)
                                {
                                    return script.path == module && script.language == ScriptModule::Language::eCSharp;
                                }))
        {
            const auto directory = module.parent_path();
            source.materialize(directory / "Vultra.Scripting.dll");
            source.materialize(directory / "Vultra.ManagedHost.dll");
            source.materialize(directory / "Vultra.ManagedHost.runtimeconfig.json");
            const std::array optional {directory / "Vultra.ManagedHost.deps.json",
                                       directory / (module.stem().string() + ".deps.json")};
            for (const auto& path : optional)
            {
                if (source.contains(path))
                {
                    source.materialize(path);
                }
            }
        }
        for (const auto& asset : assets())
        {
            auto extension = asset.path.extension().string();
            std::ranges::transform(extension,
                                   extension.begin(),
                                   [](unsigned char c)
                                   {
                                       return char(std::tolower(c));
                                   });
            if (extension == ".dll" || extension == ".so" || extension == ".dylib" ||
                asset.path.filename().string().contains(".so."))
            {
                source.materialize(asset.path);
            }
        }
        return result;
    }
} // namespace vultra
