#include "research_workspace.hpp"

#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_import.hpp>
#include <vultra/servers/rendering/builtin/raster_passes.hpp>
#include <vultra/servers/rendering/builtin/render_properties.generated.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/servers/rendering/research/graph_report.hpp>

#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <set>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        void validateDocument(const ResearchDocument& document)
        {
            const auto& settings = document.settings;
            if (document.project.empty() || document.size.empty() ||
                (settings.path != RenderPath::eNaiveDeferred && settings.path != RenderPath::eNaiveForward &&
                 settings.path != RenderPath::eReferencePathTracing) ||
                !std::isfinite(settings.exposure) || settings.exposure < -4 || settings.exposure > 4 ||
                !std::isfinite(settings.lightIntensity) || settings.lightIntensity < 0 || settings.lightIntensity > 10)
            {
                throw std::invalid_argument("Invalid research project, extent or renderer settings");
            }
            for (const auto& [id, position] : document.nodePositions)
            {
                if (id.empty() || !std::isfinite(position.x) || !std::isfinite(position.y) ||
                    std::abs(position.x) > 1000000 || std::abs(position.y) > 1000000)
                {
                    throw std::invalid_argument("Invalid graph node position: " + id);
                }
            }
            if (document.camera)
            {
                const auto& camera = *document.camera;
                if (!std::isfinite(camera.center.x) || !std::isfinite(camera.center.y) ||
                    !std::isfinite(camera.center.z) || !std::isfinite(camera.radius) || camera.radius <= 0 ||
                    !std::isfinite(camera.distance) || camera.distance <= 0 || !std::isfinite(camera.yaw) ||
                    !std::isfinite(camera.pitch) || camera.pitch < -1.5f || camera.pitch > 1.5f ||
                    !std::isfinite(camera.verticalFov) || camera.verticalFov <= 0 ||
                    camera.verticalFov >= glm::pi<float>())
                {
                    throw std::invalid_argument("Invalid research camera");
                }
            }
        }

        std::unique_ptr<ResearchGraph> compileGraph(Device&                 device,
                                                    BuiltinRenderer&        renderer,
                                                    GpuScene&               geometry,
                                                    Environment&            environment,
                                                    const PassCatalog&      catalog,
                                                    const ResearchDocument& document)
        {
            auto                      result  = std::make_unique<ResearchGraph>(device);
            auto&                     graph   = result->graph;
            auto&                     outputs = result->rendererOutputs;
            std::vector<GraphBinding> imports;
            const bool                authoredRaster = usesBuiltinRasterPasses(document.definition);
            if (authoredRaster && document.settings.path != RenderPath::eNaiveDeferred)
            {
                throw std::invalid_argument("Explicit built-in raster graphs require the indexed deferred path");
            }
            if (document.settings.path == RenderPath::eReferencePathTracing)
            {
                result->reference        = std::make_unique<ReferencePathTracer>(device, geometry, environment);
                result->referenceOutputs = result->reference->addPasses(graph, document.size);
                const auto& reference    = result->referenceOutputs;
                outputs.path             = document.settings.path;
                outputs.hdr              = reference.hdr;
                outputs.depth            = reference.depth;
                imports                  = {{"scene.hdr", reference.hdr},
                                            {"scene.radiance", reference.radiance},
                                            {"scene.albedo", reference.albedo},
                                            {"scene.normal", reference.normal},
                                            {"scene.depth", reference.depth},
                                            {"scene.motion", reference.motion},
                                            {"scene.sample_count", reference.sampleCount},
                                            {"scene.ray_count", reference.rayCount}};
            }
            else if (authoredRaster)
            {
                outputs.path          = document.settings.path;
                result->rasterCatalog = std::make_unique<PassCatalog>(catalog);
                bindBuiltinRasterPasses(*result->rasterCatalog, renderer, outputs, document.size);
            }
            else
            {
                outputs = renderer.addScenePasses(graph, document.size);
                imports = {{"scene.hdr", outputs.hdr}, {"scene.depth", outputs.depth}};
            }
            if (!authoredRaster && outputs.path == RenderPath::eNaiveDeferred)
            {
                constexpr std::array names {"scene.position_metallic",
                                            "scene.normal_roughness",
                                            "scene.albedo_weight",
                                            "scene.emission_occlusion",
                                            "scene.specular",
                                            "scene.geometric_normal_ior",
                                            "scene.coat"};
                for (size_t i = 0; i < names.size(); ++i)
                {
                    imports.push_back({names[i], outputs.gbuffer[i]});
                }
            }
            result->instances =
                document.definition.build(graph, result->rasterCatalog ? *result->rasterCatalog : catalog, imports);
            const auto hdr  = result->instances.outputs.front().resource;
            const auto info = graph.resourceInfo(hdr);
            if (!info.isTexture ||
                (info.textureDesc.format != VriFormat_RGBA16_SFLOAT &&
                 info.textureDesc.format != VriFormat_RGBA8_UNORM) ||
                info.textureDesc.width != document.size.width || info.textureDesc.height != document.size.height)
            {
                throw std::invalid_argument("First research output must be HDR or display RGBA8 at the scene extent: " +
                                            result->instances.outputs.front().name);
            }
            if (info.textureDesc.format == VriFormat_RGBA16_SFLOAT)
            {
                outputs.hdr   = hdr;
                outputs.color = renderer.addToneMappingPass(graph, hdr, document.size);
            }
            else
            {
                outputs.color = hdr;
            }
            graph.exportResource(outputs.color);
            result->previews.push_back({"display.final", outputs.color});
            if (result->reference)
            {
                for (const auto& output : imports)
                {
                    if (output.name != "scene.hdr" &&
                        std::ranges::find(result->instances.outputs, output.name, &GraphBinding::name) ==
                            result->instances.outputs.end())
                    {
                        graph.exportResource(output.resource);
                        result->previews.push_back(output);
                    }
                }
            }
            result->previews.insert(result->previews.end(),
                                    result->instances.outputs.begin(),
                                    result->instances.outputs.end());
            std::vector<RenderGraph::Use> reads;
            std::set<uint32_t>            seen;
            for (const auto& preview : result->previews)
            {
                if (graph.resourceInfo(preview.resource).isTexture && seen.insert(preview.resource.index).second)
                {
                    reads.push_back({preview.resource, Usage::eSampled});
                }
            }
            // ImGui samples these textures after graph execution. Keep their declared final states visible.
            graph.addPass(
                "Inspect outputs",
                std::span(reads),
                [](auto*, auto&)
                {
                },
                true);
            graph.compile();
            result->snapshot = graph.snapshot();
            return result;
        }
    } // namespace

    void ResearchDocument::save(const std::filesystem::path& file) const
    {
        validateDocument(*this);
        // Validate the embedded scene before publishing a replacement workspace file.
        const auto snapshot =
            sceneSnapshot.empty() ? Json(nullptr) : Json::parse(SceneTree::parse(sceneSnapshot).serialize());
        const auto root = std::filesystem::absolute(file).parent_path();
        std::filesystem::create_directories(root);
        const auto projectPath   = std::filesystem::absolute(project);
        auto       storedProject = projectPath.lexically_relative(root);
        if (storedProject.empty())
        {
            storedProject = projectPath; // Different Windows volumes cannot share a relative path.
        }
        const auto projectText = storedProject.generic_u8string();
        Json       data {{"format", "vultra.research"},
                         {"version", 1},
                         {"project", std::string(projectText.begin(), projectText.end())},
                         {"scene", snapshot},
                         {"graph", Json::parse(definition.serialize())},
                         {"extent", {size.width, size.height}},
                         {"seed", seed},
                         {"renderer", Json::parse(serializeProperties(renderSettingsType(), &settings))}};

        if (camera)
        {
            data["camera"] = {{"center", {camera->center.x, camera->center.y, camera->center.z}},
                              {"radius", camera->radius},
                              {"distance", camera->distance},
                              {"yaw", camera->yaw},
                              {"pitch", camera->pitch},
                              {"vertical_fov", camera->verticalFov}};
        }
        data["node_positions"] = Json::object();
        for (const auto& [id, position] : nodePositions)
        {
            data["node_positions"][id] = {position.x, position.y};
        }
        const auto text = data.dump(2) + '\n';
        writeFileAtomically(file, std::as_bytes(std::span(text)));
    }

    ResearchDocument ResearchDocument::load(const std::filesystem::path& file)
    {
        std::ifstream source(file, std::ios::binary);
        if (!source)
        {
            throw std::runtime_error("Cannot open research workspace: " + file.string());
        }
        try
        {
            const auto data = Json::parse(source);
            if (data.at("format") != "vultra.research" || data.at("version") != 1)
            {
                throw std::invalid_argument("Expected vultra.research version 1");
            }
            ResearchDocument document;
            const auto       text = data.at("project").get<std::string>();
            if (text.empty())
            {
                throw std::invalid_argument("Research workspace needs a project path");
            }
            const std::u8string utf8(text.begin(), text.end());
            document.project =
                (std::filesystem::absolute(file).parent_path() / std::filesystem::path(utf8)).lexically_normal();
            document.definition = GraphDefinition::parse(data.at("graph").dump());
            if (!data.at("scene").is_null())
            {
                document.sceneSnapshot = SceneTree::parse(data.at("scene").dump()).serialize();
            }
            const auto& extent = data.at("extent");
            if (!extent.is_array() || extent.size() != 2)
            {
                throw std::invalid_argument("Research extent needs two positive integer dimensions");
            }
            for (const auto& dimension : extent)
            {
                if (!dimension.is_number_integer() || dimension <= 0 || dimension > UINT32_MAX)
                {
                    throw std::invalid_argument("Research extent needs two positive integer dimensions");
                }
            }
            document.size    = {extent[0].get<uint32_t>(), extent[1].get<uint32_t>()};
            const auto& seed = data.at("seed");
            if (!seed.is_number_unsigned() || seed > UINT32_MAX)
            {
                throw std::invalid_argument("Research seed must fit uint32");
            }
            document.seed = seed.get<uint32_t>();
            deserializeProperties(renderSettingsType(), data.at("renderer").dump(), &document.settings);
            if (data.contains("camera"))
            {
                const auto& value            = data.at("camera");
                const auto  center           = value.at("center").get<std::array<float, 3>>();
                document.camera              = OrbitCamera {};
                document.camera->center      = {center[0], center[1], center[2]};
                document.camera->radius      = value.at("radius").get<float>();
                document.camera->distance    = value.at("distance").get<float>();
                document.camera->yaw         = value.at("yaw").get<float>();
                document.camera->pitch       = value.at("pitch").get<float>();
                document.camera->verticalFov = value.at("vertical_fov").get<float>();
            }
            if (!data.at("node_positions").is_object())
            {
                throw std::invalid_argument("Graph node positions must be an object");
            }
            for (const auto& [id, value] : data.at("node_positions").items())
            {
                const auto position = value.get<std::array<float, 2>>();
                document.nodePositions.emplace(id, glm::vec2(position[0], position[1]));
            }
            validateDocument(document);
            return document;
        }
        catch (const Json::exception& error)
        {
            throw std::invalid_argument("Invalid research workspace " + file.string() + ": " + error.what());
        }
    }

    ResearchWorkspace::ResearchWorkspace(Device& device, RenderingServer& server, const PassCatalog& catalog) :
        m_Device(device),
        m_Server(server),
        m_Catalog(catalog)
    {
    }

    void ResearchWorkspace::replace(const ResearchDocument& document, const BeforeReplace& releasePreviews)
    {
        validateDocument(document);
        const auto             projectPath = std::filesystem::absolute(document.project).lexically_normal();
        std::unique_ptr<Scene> candidateScene;
        auto*                  scene = m_Scene.get();
        if (!scene || scene->projectPath != projectPath ||
            (!document.sceneSnapshot.empty() && document.sceneSnapshot != scene->tree->serialize()))
        {
            const auto project = ProjectManifest::load(projectPath);
            if (!project.scripts.empty() || !project.extensions.empty())
            {
                throw std::invalid_argument("Research workspaces currently require static projects");
            }
            auto tree = document.sceneSnapshot.empty() ?
                            SceneTree::load(projectPath.parent_path() / project.mainScene) :
                            SceneTree::parse(document.sceneSnapshot);
            tree.validateAssets(project);
            SceneRenderState renderState;
            renderState.update(tree, document.size);
            std::vector<SceneMeshInstance> instances;
            const auto asset       = importScene(tree, project, projectPath.parent_path(), {}, &instances);
            candidateScene         = std::make_unique<Scene>();
            scene                  = candidateScene.get();
            scene->projectPath     = projectPath;
            scene->project         = project;
            scene->tree            = std::make_unique<SceneTree>(std::move(tree));
            scene->renderState     = std::move(renderState);
            scene->instances       = std::move(instances);
            scene->gpu             = m_Server.uploadScene(asset);
            const auto environment = sceneEnvironmentPath(*scene->tree, project, projectPath.parent_path());
            scene->environment     = std::make_unique<Environment>(m_Device, environment);
            scene->renderer        = std::make_unique<BuiltinRenderer>(m_Device, *scene->gpu, *scene->environment);
        }
        auto&      renderer = *scene->renderer;
        const auto previous = renderer.settings;
        renderer.settings   = document.settings;
        std::unique_ptr<ResearchGraph> candidate;
        try
        {
            candidate = compileGraph(m_Device, renderer, *scene->gpu, *scene->environment, m_Catalog, document);
        }
        catch (...)
        {
            renderer.settings = previous;
            throw;
        }
        renderer.settings = previous;
        // Retire preview descriptors only after a fully compiled replacement exists.
        if (m_Graph && releasePreviews)
        {
            releasePreviews(*m_Graph);
        }
        m_Graph.reset();
        if (candidateScene)
        {
            m_Scene = std::move(candidateScene);
        }
        m_Graph                     = std::move(candidate);
        m_Document                  = document;
        m_Document.project          = projectPath;
        m_Scene->renderer->settings = document.settings;
        if (document.camera)
        {
            m_Camera = *document.camera;
        }
        else
        {
            m_Camera          = OrbitCamera {};
            m_Camera.center   = m_Scene->gpu->center;
            m_Camera.radius   = m_Scene->gpu->radius;
            m_Camera.distance = m_Camera.radius * 2.5f;
            m_Camera.pitch    = 0.2f;
        }
        m_Server.collectCompletedFrame();
    }

    void ResearchWorkspace::setPassParameters(std::string_view id, const PassParameters& parameters)
    {
        auto&      active = graph();
        const auto pass   = std::ranges::find(active.instances.passes, id, &BuiltPass::name);
        const auto saved  = std::ranges::find(m_Document.definition.passes, id, &GraphPassDesc::id);
        if (pass == active.instances.passes.end() || saved == m_Document.definition.passes.end())
        {
            throw std::invalid_argument("Unknown active pass: " + std::string(id));
        }
        auto stored = parameters;
        m_Catalog.setParameters(*pass, stored);
        saved->parameters.swap(stored);
    }

    void ResearchWorkspace::setEnvironment(ObjectId node, AssetId radiance)
    {
        auto&            tree        = scene();
        EnvironmentNode* environment = nullptr;
        AssetId          asset {};
        if (node.value)
        {
            auto* candidate = tree.find(node);
            if (!candidate || candidate->kind() != NodeKind::eEnvironment)
            {
                throw std::invalid_argument("Select an environment node in the active scene");
            }
            environment = static_cast<EnvironmentNode*>(candidate);
            asset       = radiance;
        }
        else if (m_Scene->project.environment)
        {
            asset = *m_Scene->project.environment;
        }
        std::filesystem::path source;
        if (asset.value.valid())
        {
            const auto& path = m_Scene->project.asset(asset).path;
            if (path.extension() != ".hdr")
            {
                throw std::invalid_argument("Environment asset must be a Radiance HDR image");
            }
            source = m_Scene->projectPath.parent_path() / path;
        }
        m_Scene->environment->setSource(source);
        if (environment)
        {
            environment->setRadianceAsset(radiance);
        }
        tree.setCurrentEnvironment(node);
    }

    const ProjectManifest& ResearchWorkspace::project() const
    {
        if (!m_Scene)
        {
            throw std::logic_error("Research workspace has no active scene");
        }
        return m_Scene->project;
    }

    uint32_t ResearchWorkspace::materialSlotCount(ObjectId mesh) const
    {
        const auto* node = m_Scene->tree->find(mesh);
        if (!node || node->kind() != NodeKind::eMeshInstance)
        {
            throw std::invalid_argument("Select a mesh in the active scene");
        }
        const auto instance = std::ranges::find(m_Scene->instances, node->idInScene(), &SceneMeshInstance::node);
        if (instance == m_Scene->instances.end())
        {
            throw std::invalid_argument("Mesh material slots require importing this scene instance");
        }
        return uint32_t(instance->importedMaterials.size());
    }

    const ShaderAsset& ResearchWorkspace::shaderAsset(ObjectId material)
    {
        if (!m_Scene->shaderMaterials)
        {
            m_Scene->shaderMaterials =
                std::make_unique<SceneShaderMaterials>(m_Device, m_Scene->project, m_Scene->projectPath.parent_path());
        }
        const auto* resource = m_Scene->tree->findMaterial(material);
        if (!resource)
        {
            throw std::invalid_argument("Shader material was removed");
        }
        return m_Scene->shaderMaterials->asset(resource->shaderMaterial());
    }

    std::string ResearchWorkspace::shaderDiagnostics() const
    {
        return m_Scene->shaderMaterials ? m_Scene->shaderMaterials->diagnostics() : std::string {};
    }

    void ResearchWorkspace::prepareFrame()
    {
        auto& active = graph();
        if (m_Scene->gpuSync.environmentChanged(*m_Scene->tree))
        {
            m_Scene->environment->setSource(
                sceneEnvironmentPath(*m_Scene->tree, m_Scene->project, m_Scene->projectPath.parent_path()));
        }
        m_Scene->gpuSync.update(*m_Scene->tree, m_Scene->instances, *m_Scene->gpu);
        if (SceneShaderMaterials::containsShaders(*m_Scene->tree))
        {
            if (active.reference)
            {
                throw std::invalid_argument(
                    "Game Surface materials require a raster render path; RayQuery uses raw Slang");
            }
            if (!m_Scene->shaderMaterials)
            {
                m_Scene->shaderMaterials = std::make_unique<SceneShaderMaterials>(m_Device,
                                                                                  m_Scene->project,
                                                                                  m_Scene->projectPath.parent_path());
            }
        }
        if (m_Scene->shaderMaterials)
        {
            m_Scene->shaderMaterials->update(*m_Scene->tree,
                                             m_Scene->instances,
                                             *m_Scene->renderer,
                                             active.graph,
                                             active.rendererOutputs);
        }
        m_Scene->renderState.update(*m_Scene->tree, m_Document.size);
        const auto camera = m_Scene->renderState.camera.value_or(m_Camera.camera(m_Document.size));
        if (active.reference)
        {
            const auto&       settings = m_Scene->renderer->settings;
            const RenderLight fallback {RenderLightKind::eDirectional,
                                        {},
                                        settings.directionToLight,
                                        settings.lightColor,
                                        settings.lightIntensity};
            const auto lights = m_Scene->renderState.lighting().value_or(std::span<const RenderLight>(&fallback, 1));
            active.reference->shader().poll();
            active.reference->prepare(camera,
                                      active.graph,
                                      active.referenceOutputs,
                                      lights,
                                      m_Scene->renderState.environmentIntensity * settings.environmentIntensity,
                                      m_Document.seed);
            m_Scene->renderer->prepareToneMapping(active.graph, active.rendererOutputs.hdr);
        }
        else
        {
            m_Scene->renderer->prepare(camera,
                                       active.graph,
                                       active.rendererOutputs,
                                       m_Scene->renderState.lighting(),
                                       m_Scene->renderState.environmentIntensity);
        }
    }

    void ResearchWorkspace::record(VriCommandBuffer* cmd, Profiler* profiler)
    {
        graph().graph.execute(cmd, profiler);
    }

    void ResearchWorkspace::completeFrame()
    {
        if (graph().reference)
        {
            graph().reference->completeFrame();
        }
    }

    void ResearchWorkspace::save(const std::filesystem::path& file) const
    {
        auto document          = m_Document;
        document.camera        = m_Camera;
        document.sceneSnapshot = m_Scene->tree->serialize();
        document.save(file);
    }

    void ResearchWorkspace::exportImages(const std::filesystem::path& directory, std::span<const PassTiming> timings)
    {
        auto& active = graph();
        if (directory.empty() || std::filesystem::exists(directory))
        {
            throw std::invalid_argument("Use a new research capture directory");
        }
        // Check all marked resources before creating a partial capture directory.
        for (const auto& output : active.previews)
        {
            if (!active.graph.resourceInfo(output.resource).isTexture)
            {
                throw std::invalid_argument("Image export requires a texture output: " + output.name);
            }
        }
        std::filesystem::create_directories(directory);
        save(directory / "workspace.vworkspace");
        m_Document.definition.save(directory / "graph.vgraph");
        Json manifest {{"format", "vultra.research.capture"}, {"version", 1}, {"outputs", Json::array()}};
        for (size_t i = 0; i < active.previews.size(); ++i)
        {
            const auto  stem   = i == 0 ? "final" : std::format("output_{:03}", i - 1);
            const auto& output = active.previews[i];
            const auto  image  = readback(m_Device, active.graph.getTexture(output.resource));
            savePng(image, directory / (stem + ".png"));
            savePfm(image, directory / (stem + ".pfm"));
            manifest["outputs"].push_back({{"port", output.name}, {"png", stem + ".png"}, {"pfm", stem + ".pfm"}});
        }
        std::vector<std::string> files;
        files.reserve(active.previews.size());
        for (size_t i = 0; i < active.previews.size(); ++i)
        {
            files.push_back(i == 0 ? "final.pfm" : std::format("output_{:03}.pfm", i - 1));
        }
        std::vector<GraphCapture> images;
        for (size_t i = 0; i < active.previews.size(); ++i)
        {
            images.push_back({active.previews[i].name, files[i], active.previews[i].resource});
        }
        const auto diagnostics = graphReport(active.graph, timings, images);
        writeFileAtomically(directory / "graph_report.json", std::as_bytes(std::span(diagnostics)));
        const auto text = manifest.dump(2) + '\n';
        writeFileAtomically(directory / "manifest.json", std::as_bytes(std::span(text)));
    }

    const ResearchDocument& ResearchWorkspace::document() const
    {
        return m_Document;
    }

    ResearchGraph& ResearchWorkspace::graph()
    {
        if (!m_Graph)
        {
            throw std::logic_error("No compiled research workspace");
        }
        return *m_Graph;
    }

    OrbitCamera& ResearchWorkspace::camera()
    {
        return m_Camera;
    }

    SceneTree& ResearchWorkspace::scene()
    {
        if (!m_Scene)
        {
            throw std::logic_error("No loaded research scene");
        }
        return *m_Scene->tree;
    }

    GpuSceneRid ResearchWorkspace::sceneRid() const
    {
        if (!m_Scene)
        {
            throw std::logic_error("No loaded research scene");
        }
        return m_Scene->gpu.rid();
    }
} // namespace vultra
