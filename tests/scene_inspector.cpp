#include "../editor/src/scene_inspector.hpp"

#include <vultra/api/scene_properties.generated.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    ImVec2 itemCenter()
    {
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    }

    // Custom drawers are a public Inspector extension point. Capture real widget bounds for pointer tests.
    struct FloatProbe
    {
        ImVec2               position;
        float                min = 0;
        float                max = 4;
        std::optional<float> assigned;

        static bool draw(vultra::EditorGuiProperty, void* value, void* data)
        {
            auto& probe   = *static_cast<FloatProbe*>(data);
            auto& number  = *static_cast<float*>(value);
            bool  changed = false;
            if (auto assigned = std::exchange(probe.assigned, std::nullopt))
            {
                number  = *assigned;
                changed = true;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::SliderFloat("##value", &number, probe.min, probe.max);
            probe.position = itemCenter();
            return changed;
        }
    };

    struct VectorProbe
    {
        ImVec2                   position;
        std::optional<glm::vec3> assigned;

        static bool draw(vultra::EditorGuiProperty, void* value, void* data)
        {
            auto& probe   = *static_cast<VectorProbe*>(data);
            auto* vector  = static_cast<float*>(value);
            bool  changed = false;
            if (auto assigned = std::exchange(probe.assigned, std::nullopt))
            {
                for (int i = 0; i < 3; ++i)
                {
                    vector[i] = (*assigned)[i];
                }
                changed = true;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::DragFloat3("##value", vector, 0.01f);
            const auto min = ImGui::GetItemRectMin();
            const auto max = ImGui::GetItemRectMax();
            probe.position = {min.x + (max.x - min.x) / 6, (min.y + max.y) * 0.5f};
            return changed;
        }
    };

    struct BoolProbe
    {
        ImVec2 position;

        static bool draw(vultra::EditorGuiProperty, void* value, void* data)
        {
            const bool changed                      = ImGui::Checkbox("##value", static_cast<bool*>(value));
            static_cast<BoolProbe*>(data)->position = itemCenter();
            return changed;
        }
    };
} // namespace

int main()
try
{
    using namespace vultra;
    const auto directory = std::filesystem::absolute(std::filesystem::path("build/.tmp") /
                                                     ("scene-inspector-" + StableId::generate().toString()));
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "quad.obj") << "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\n"
                                             "vn 0 0 1\nf 1//1 2//1 3//1\nf 1//1 3//1 4//1\n";
    {
        std::ofstream hdr(directory / "constant.hdr", std::ios::binary);
        hdr << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
        const unsigned char pixel[4] {128, 64, 32, 129};
        for (int i = 0; i < 8; ++i)
        {
            hdr.write(reinterpret_cast<const char*>(pixel), 4);
        }
    }
    ProjectManifest project;
    project.mainScene   = "main.vscene";
    const auto model    = project.addAsset("quad.obj");
    const auto radiance = project.addAsset("constant.hdr");
    const auto missing  = project.addAsset("missing.hdr");
    project.environment = radiance;
    project.save(directory / "project.vproject");
    SceneTree initial(std::make_unique<Node>("World"));
    initial.addChild(initial.root(), std::make_unique<MeshInstanceNode>("Quad", model));
    initial.save(directory / "main.vscene");

    VectorProbe      position;
    VectorProbe      rotation;
    VectorProbe      scale;
    FloatProbe       fov {{}, 0.1f, 3};
    FloatProbe       nearPlane;
    FloatProbe       intensity {{}, 0, 20};
    FloatProbe       environmentIntensity;
    FloatProbe       red;
    BoolProbe        cameraSelection;
    BoolProbe        environmentSelection;
    Device           device;
    RenderingServer  server(device);
    EditorGui        gui(device, VriFormat_RGBA8_UNORM, {.multiViewport = false, .persistLayout = false});
    const std::array drawers {
        std::pair {"Node.position", EditorGuiPropertyDrawer {VectorProbe::draw, &position}},
        std::pair {"Node.rotation", EditorGuiPropertyDrawer {VectorProbe::draw, &rotation}},
        std::pair {"Node.scale", EditorGuiPropertyDrawer {VectorProbe::draw, &scale}},
        std::pair {"CameraSettings.verticalFov", EditorGuiPropertyDrawer {FloatProbe::draw, &fov}},
        std::pair {"CameraSettings.nearPlane", EditorGuiPropertyDrawer {FloatProbe::draw, &nearPlane}},
        std::pair {"LightSettings.intensity", EditorGuiPropertyDrawer {FloatProbe::draw, &intensity}},
        std::pair {"EnvironmentSettings.intensity", EditorGuiPropertyDrawer {FloatProbe::draw, &environmentIntensity}},
        std::pair {"MaterialParameters.baseRed", EditorGuiPropertyDrawer {FloatProbe::draw, &red}},
        std::pair {"Camera.current", EditorGuiPropertyDrawer {BoolProbe::draw, &cameraSelection}},
        std::pair {"Environment.current", EditorGuiPropertyDrawer {BoolProbe::draw, &environmentSelection}}};
    for (const auto& [id, drawer] : drawers)
    {
        gui.setPropertyDrawer(id, drawer);
    }
    SceneInspector   inspector(gui);
    PassCatalog      catalog(device);
    ResearchDocument document;
    document.project            = directory / "project.vproject";
    document.size               = {129, 97};
    document.definition.outputs = {"scene.hdr"};
    ResearchWorkspace workspace(device, server, catalog);
    workspace.replace(document);
    auto*        active = &workspace;
    const Extent extent {900, 1000};
    Texture      target(device, colorTexture(extent));
    Frame        frame(device);
    ImVec2       lastTreeItem;
    std::string  status;
    const auto   render = [&]
    {
        status.clear();
        try
        {
            inspector.applyPending(*active);
        }
        catch (const std::exception& error)
        {
            status = error.what();
        }
        gui.begin(extent, 1.0f / 60);
        auto ui = gui.frame();
        ui.setNextWindowPos({12, 12}, ImGuiCond_Always);
        ui.setNextWindowSize({250, 976}, ImGuiCond_Always);
        {
            EditorGuiWindow window(ui, "Scene tree");
            inspector.drawTree(ui, active->scene());
            lastTreeItem = itemCenter();
        }
        ui.setNextWindowPos({274, 12}, ImGuiCond_Always);
        ui.setNextWindowSize({614, 976}, ImGuiCond_Always);
        {
            EditorGuiWindow window(ui, "Inspector");
            try
            {
                inspector.drawProperties(ui, *active);
            }
            catch (const std::exception& error)
            {
                status = error.what();
            }
        }
        gui.upload(extent);
        active->prepareFrame();
        auto* cmd = frame.begin();
        active->record(cmd);
        gui.copy(cmd);
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 0, 1};
        beginColorPass(device, cmd, target.view(), extent, clear);
        gui.draw(cmd);
        device.core.CmdEndRendering(cmd);
        frame.submitAndWait();
        active->completeFrame();
    };
    const auto image = [&]
    {
        return readback(device, active->graph().graph.getTexture(active->graph().rendererOutputs.color));
    };
    const auto click = [&](ImVec2 point)
    {
        ImGui::GetIO().AddMousePosEvent(point.x, point.y);
        render();
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        render();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        render();
    };
    const auto hold = [&](ImVec2 point)
    {
        ImGui::GetIO().AddMousePosEvent(point.x, point.y);
        render();
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        render();
    };
    const auto release = [&]
    {
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        render();
    };
    render();
    render();
    auto& mesh = *workspace.scene().root().children().front();
    click(lastTreeItem);
    const auto  rid      = workspace.sceneRid();
    auto*       graph    = &workspace.graph();
    const auto* output   = graph->graph.getTexture(graph->rendererOutputs.color).handle;
    const auto  baseline = image();
    hold(position.position);
    ImGui::GetIO().AddMousePosEvent(position.position.x + 35, position.position.y);
    render();
    require(ImGui::GetIO().MouseDown[0] && mesh.localTransform()[3].x != 0 && image().rgba != baseline.rgba,
            "Mesh movement did not preview during a held Inspector drag");
    release();
    const auto transform = mesh.localTransform();
    scale.assigned       = glm::vec3(0);
    render();
    require(!status.empty() && mesh.localTransform() == transform,
            "Singular Inspector scale changed the active scene instead of rejecting the edit");
    scale.assigned = glm::vec3(0.001f);
    render();
    position.assigned = glm::vec3(0.5f, 0, 0);
    render();
    require(status.empty() && mesh.localTransform()[3].x == 0.5f,
            "A small valid scale disabled Inspector transform editing");
    scale.assigned = glm::vec3(-1, 2, 1);
    render();
    require(status.empty() && mesh.localTransform()[0].x < 0 && mesh.localTransform()[1].y == 2,
            "Signed nonuniform scale did not recover after a rejected edit");
    auto shear = mesh.localTransform();
    shear[1].x = 0.3f;
    mesh.setLocalTransform(shear);
    render();
    const auto oldShear = glm::dot(glm::normalize(glm::vec3(shear[0])), glm::normalize(glm::vec3(shear[1])));
    rotation.assigned   = glm::vec3(0, 0, 15);
    render();
    const auto rotated = mesh.localTransform();
    require(std::abs(glm::dot(glm::normalize(glm::vec3(rotated[0])), glm::normalize(glm::vec3(rotated[1]))) -
                     oldShear) < 0.0001f,
            "Inspector rotation discarded the existing shear");
    mesh.setLocalTransform(shear);

    auto& camera = static_cast<CameraNode&>(
        workspace.scene().addChild(workspace.scene().root(), std::make_unique<CameraNode>("Camera")));
    camera.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 3}));
    render();
    click(lastTreeItem);
    click(cameraSelection.position);
    require(workspace.scene().currentCamera() == camera.id(), "Inspector camera selection did not update the tree");
    const auto beforeFov = image();
    hold(fov.position);
    require(ImGui::GetIO().MouseDown[0] && image().rgba != beforeFov.rgba,
            "Generated camera field did not preview while the pointer was held");
    release();
    const auto acceptedCamera = camera.settings();
    nearPlane.assigned        = 1000;
    render();
    require(!status.empty() && camera.settings() == acceptedCamera,
            "Invalid camera settings escaped the Inspector validation boundary");
    nearPlane.assigned = 0.05f;
    render();
    require(status.empty() && camera.settings().nearPlane == 0.05f, "Camera field did not recover after rejection");

    auto& light = static_cast<LightNode&>(
        workspace.scene().addChild(workspace.scene().root(),
                                   std::make_unique<LightNode>("Point", RenderLightKind::ePoint)));
    light.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 2}));
    render();
    click(lastTreeItem);
    const auto beforeLight = image();
    hold(intensity.position);
    require(ImGui::GetIO().MouseDown[0] && image().rgba != beforeLight.rgba,
            "Generated light field did not change the next HDR/display frame while held");
    release();

    auto& environment = static_cast<EnvironmentNode&>(
        workspace.scene().addChild(workspace.scene().root(), std::make_unique<EnvironmentNode>("Environment")));
    environment.setRadianceAsset(missing);
    render();
    const auto environmentTreeItem = lastTreeItem;
    click(lastTreeItem);
    const auto beforeSource = image();
    click(environmentSelection.position);
    require(workspace.scene().currentEnvironment().value == 0,
            "HDR replacement ran inside GUI construction rather than the completion boundary");
    render();
    require(!status.empty() && workspace.scene().currentEnvironment().value == 0 && image().rgba == beforeSource.rgba,
            "Failed HDR selection damaged the active lighting/scene selection");
    environment.setRadianceAsset(radiance);
    render();
    click(environmentSelection.position);
    render();
    require(status.empty() && workspace.scene().currentEnvironment() == environment.id(),
            "Corrected HDR selection did not recover at the completed-frame boundary");
    const auto beforeEnvironment = image();
    hold(environmentIntensity.position);
    require(ImGui::GetIO().MouseDown[0] && image().rgba != beforeEnvironment.rgba,
            "Generated environment field did not preview while held");
    release();

    auto& material = workspace.scene().addMaterial(std::make_unique<MaterialResource>("Shared material"));
    static_cast<MeshInstanceNode&>(mesh).setMaterial(0, material.assetId());
    render();
    click(lastTreeItem);
    const auto beforeMaterial = image();
    hold(red.position);
    require(ImGui::GetIO().MouseDown[0] && image().rgba != beforeMaterial.rgba,
            "Generated material field did not preview while held");
    release();
    require(workspace.sceneRid() == rid && &workspace.graph() == graph &&
                graph->graph.getTexture(graph->rendererOutputs.color).handle == output,
            "Inspector edits replaced geometry, the graph or its textures");
    const auto saved = image();
    workspace.save(directory / "saved.vworkspace");
    ResearchWorkspace reopened(device, server, catalog);
    reopened.replace(ResearchDocument::load(directory / "saved.vworkspace"));
    click(environmentTreeItem);
    click(environmentSelection.position);
    active = &reopened;
    render();
    require(reopened.scene().currentEnvironment().value != 0 && image().rgba == saved.rgba &&
                reopened.scene().serialize() == workspace.scene().serialize(),
            "Inspector-authored scene state changed after saved workspace reload");
    savePng(readback(device, target), directory / "inspector.png");
    for (const auto& [id, drawer] : drawers)
    {
        gui.removePropertyDrawer(id);
    }
    std::cout << "Scene Inspector tests passed: tree/resource selection, live GPU edits, reflection/drawers, signed "
                 "scale/shear, rejected edits, HDR failure/recovery, resource reuse and saved-image parity; captures "
              << directory << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
