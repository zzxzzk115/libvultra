#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/main/experiment_session.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
{
    using namespace vultra;
    namespace fs    = std::filesystem;
    const auto root = fs::absolute(fs::path("build/.tmp/game-project") / StableId::generate().toString());
    fs::create_directories(root);
    fs::copy_file("resources/models/DamagedHelmet/DamagedHelmet.glb", root / "helmet.glb");
    fs::copy_file("tests/shaders/painted_metal.vshader", root / "painted.vshader");
    ProjectManifest project;
    const auto      model  = project.addAsset("helmet.glb");
    const auto      shader = project.addAsset("painted.vshader");
    SceneTree       tree(std::make_unique<Node>("Game shader project"));
    auto&           mesh =
        static_cast<MeshInstanceNode&>(tree.addChild(tree.root(), std::make_unique<MeshInstanceNode>("Helmet", model)));
    auto&            resource = tree.addMaterial(std::make_unique<MaterialResource>("Painted metal"));
    MaterialInstance instance;
    instance.shader = shader;
    resource.setShaderMaterial(instance);
    mesh.setMaterial(0, resource.assetId());
    tree.save(root / "main.vscene");
    project.mainScene = "main.vscene";
    project.save(root / "project.vproject");
    const auto restored = SceneTree::load(root / "main.vscene");
    require(restored.materials()[0]->kind() == MaterialResource::Kind::eShader &&
                restored.materials()[0]->shaderMaterial().shader == shader,
            "Scene persistence lost the shader material category or AssetId");
    bool wrongKind = false;
    try
    {
        resource.parameters();
    }
    catch (const std::logic_error&)
    {
        wrongKind = true;
    }
    require(wrongKind, "Numeric material API accepted a shader material");

    Device device;
    Image  baseline;
    {
        ExperimentConfig config {.input = root / "project.vproject",
                                 .size  = {64, 48},
                                 .path  = RenderPath::eNaiveForward};
        config.importOptions.cacheDirectory = root / "cache";
        ExperimentSession session(device, config);
        session.render();
        baseline                     = session.capture("hdr");
        const auto           texture = session.output("hdr").handle;
        auto&                live    = *session.scene()->materials()[0];
        auto                 edited  = live.shaderMaterial();
        ShaderCompileOptions options;
        options.includeDirectories = {"builtin/shaders", "external"};
        const auto asset           = ShaderAsset::compile(root / "painted.vshader", options);
        edited.set(asset, "emission", std::array<float, 4> {4, 0, 0, 1});
        live.setShaderMaterial(edited);
        session.render();
        require(compare(baseline, session.capture("hdr")).mse > 0.01 && session.output("hdr").handle == texture,
                "Scene shader edit did not change pixels or recreated the graph");
        live.setShaderMaterial(instance);
        session.render();
        require(compare(baseline, session.capture("hdr")).mse == 0,
                "Resetting scene shader overrides changed rendering");
    }

    ShaderCompileOptions options;
    options.includeDirectories = {"builtin/shaders", "external"};
    const auto pack            = root / "game.vpk";
    VpkArchive::packProject(root / "project.vproject", pack, options);
    // Exercise native DLL classification without requiring a Windows loader on this machine.
    const std::string moduleProbe = "native module materialization probe";
    writeFileAtomically(root / "platform.dll", std::as_bytes(std::span(moduleProbe)));
    auto probeProject = project;
    probeProject.extensions.push_back("platform.dll");
    probeProject.save(root / "probe.vproject");
    const auto probePack = root / "native-module.vpk";
    VpkArchive::packProject(root / "probe.vproject", probePack, options);
    VpkArchive archive(pack);
    require(archive.contains("painted.vshaderc") && !archive.contains("painted.vshader"),
            "Game VPK retained shader source");
    AssetSource assets(archive);
    fs::remove(root / "painted.vshader");
    const auto shipped = ProjectManifest::load(assets.resolve("project.vproject"), &assets);
    require(shipped.asset(shader).path == "painted.vshaderc",
            "Shader cooking changed AssetId or failed to rewrite its path");
    {
        ExperimentConfig config {.input = pack, .size = {64, 48}, .path = RenderPath::eNaiveForward};
        config.importOptions.cacheDirectory = root / "shipped-cache";
        ExperimentSession session(device, config);
        session.render();
        require(compare(baseline, session.capture("hdr")).mse == 0, "Source-free game VPK changed GPU pixels");
        require(session.assetSource() && !fs::exists(session.projectRoot() / "helmet.glb") &&
                    !fs::exists(session.projectRoot() / "painted.vshaderc"),
                "Packaged scene or shader was extracted before rendering");
    }
    std::filesystem::path materializedModule;
    {
        ExperimentConfig config {.input = probePack, .size = {64, 48}, .path = RenderPath::eNaiveForward};
        config.importOptions.cacheDirectory = root / "probe-cache";
        ExperimentSession session(device, config);
        materializedModule = session.scriptPath("platform.dll");
        require(readSourceFile(materializedModule) ==
                        std::vector<std::byte>(std::as_bytes(std::span(moduleProbe)).begin(),
                                               std::as_bytes(std::span(moduleProbe)).end()) &&
                    !fs::exists(materializedModule.parent_path() / "helmet.glb"),
                "Materializing a native DLL required a managed host or extracted unrelated assets");
    }
    require(!fs::exists(materializedModule), "Session shutdown retained materialized module files");
    std::puts("Game project passed: typed scene persistence, live parameter edits and source-free VPK GPU parity");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
