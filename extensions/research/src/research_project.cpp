#include <vultra/research/research_project.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        AssetId assetId(const std::string& text)
        {
            const auto id = StableId::parse(text);
            if (!id)
            {
                throw std::invalid_argument("Invalid research asset ID");
            }
            return {*id};
        }
    } // namespace

    void ResearchProject::validate(const ProjectManifest& project) const
    {
        if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name == "." || name == ".." ||
            methods.empty() || width < 11 || height < 11 || width > 8192 || height > 8192)
        {
            throw std::invalid_argument("Research project requires a name, methods and valid eye extent");
        }
        if (renderPath != "forward" && renderPath != "deferred")
        {
            throw std::invalid_argument("Research render_path must be forward or deferred");
        }
        const auto renderer = nlohmann::json::parse(rendererSettings);
        if (!renderer.is_object() || renderer.contains("path"))
        {
            throw std::invalid_argument("Research renderer must be an object; use render_path to select the path");
        }
        project.asset(comparison);
        if (configurationLabel.empty() ||
            (!referenceMethod.empty() &&
             (methods.size() < 2 ||
              std::ranges::find(methods, referenceMethod, &ResearchMethod::name) == methods.end())))
        {
            throw std::invalid_argument("Research reference method must name an existing method with an alternative");
        }
        for (size_t i = 0; i < methods.size(); ++i)
        {
            const auto& method = methods[i];
            project.asset(method.graph);
            if (method.name.empty())
            {
                throw std::invalid_argument("Research method name is empty");
            }
            for (size_t j = 0; j < i; ++j)
            {
                if (methods[j].name == method.name)
                {
                    throw std::invalid_argument("Duplicate research method name");
                }
            }
        }
    }

    void ResearchProject::write(ProjectManifest& project) const
    {
        validate(project);
        auto metadata = nlohmann::json::parse(project.metadata);
        if (!metadata.is_object())
        {
            throw std::invalid_argument("Project metadata must be a JSON object");
        }
        auto& description = metadata["research"];
        description       = {{"name", name},
                             {"size", {width, height}},
                             {"features", features},
                             {"render_path", renderPath},
                             {"reference_method", referenceMethod},
                             {"configuration_label", configurationLabel},
                             {"renderer", nlohmann::json::parse(rendererSettings)},
                             {"comparison", comparison.value.toString()},
                             {"methods", nlohmann::json::array()}};
        for (const auto& method : methods)
        {
            description["methods"].push_back({{"name", method.name}, {"graph", method.graph.value.toString()}});
        }
        project.metadata = metadata.dump();
    }

    std::optional<ResearchProject> ResearchProject::read(const ProjectManifest& project)
    {
        const auto metadata = nlohmann::json::parse(project.metadata);
        if (!metadata.is_object())
        {
            throw std::invalid_argument("Project metadata must be a JSON object");
        }
        if (!metadata.contains("research"))
        {
            return std::nullopt;
        }
        const auto&     description = metadata.at("research");
        ResearchProject research;
        research.name               = description.at("name").get<std::string>();
        research.width              = description.at("size").at(0).get<uint32_t>();
        research.height             = description.at("size").at(1).get<uint32_t>();
        research.features           = description.value("features", uint64_t(0));
        research.renderPath         = description.value("render_path", std::string("forward"));
        research.referenceMethod    = description.value("reference_method", std::string {});
        research.configurationLabel = description.value("configuration_label", std::string("Configuration"));
        research.rendererSettings   = description.value("renderer", nlohmann::json::object()).dump();
        research.comparison         = assetId(description.at("comparison").get<std::string>());
        for (const auto& method : description.at("methods"))
        {
            research.methods.push_back(
                {method.at("name").get<std::string>(), assetId(method.at("graph").get<std::string>())});
        }
        research.validate(project);
        return research;
    }
} // namespace vultra
