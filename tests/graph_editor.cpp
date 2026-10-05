#include "../editor/src/graph_editor.hpp"
#include "../examples/research/color_gain.hpp"

#include <vultra/core/base/stable_id.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

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
    Device          device;
    RenderingServer server(device);
    EditorGui       gui(device, VriFormat_RGBA8_UNORM, {.multiViewport = false, .persistLayout = false});
    PassCatalog     catalog(device);
    catalog.add(research::colorGainDefinition());
    GraphEditor                        editor(catalog);
    std::unique_ptr<ResearchWorkspace> workspace;
    ResearchDocument                   document;
    document.project            = std::filesystem::absolute("resources/research.vproject");
    const auto type             = research::colorGainDefinition().type;
    document.definition.passes  = {{"gain", type, {}}, {"second", type, {}}};
    document.definition.outputs = {"scene.hdr"};
    const Extent extent {1400, 800};
    Texture      target(device, colorTexture(extent));
    Frame        frame(device);
    std::string  status;
    unsigned     changes          = 0;
    unsigned     parameterChanges = 0;
    std::string  preview;
    const auto   render = [&]
    {
        gui.begin(extent, 1.0f / 60);
        auto ui = gui.frame();
        ui.setNextWindowPos({20, 20}, ImGuiCond_Always);
        ui.setNextWindowSize({1360, 760}, ImGuiCond_Always);
        {
            EditorGuiWindow window(ui,
                                   "Graph interaction",
                                   nullptr,
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            const auto      edit = editor.draw(ui, document, status);
            changes += edit.changed;
            parameterChanges += unsigned(edit.parameterEdits.size());
            for (const auto& id : edit.parameterEdits)
            {
                if (workspace)
                {
                    const auto pass = std::ranges::find(document.definition.passes, id, &GraphPassDesc::id);
                    workspace->setPassParameters(id, pass->parameters);
                }
            }
            if (!edit.preview.empty())
            {
                preview = edit.preview;
            }
        }
        gui.upload(extent);
        auto* cmd = frame.begin();
        if (workspace)
        {
            workspace->record(cmd);
        }
        gui.copy(cmd);
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 0, 1};
        beginColorPass(device, cmd, target.view(), extent, clear);
        gui.draw(cmd);
        device.core.CmdEndRendering(cmd);
        frame.submitAndWait();
        if (workspace)
        {
            workspace->completeFrame();
        }
    };
    for (int i = 0; i < 4; ++i)
    {
        render();
    }
    const auto port = [&](std::string_view path, bool input = false)
    {
        const auto position = editor.portPosition(path, input);
        require(position.has_value(), "Canvas omitted a declared port");
        return *position;
    };
    const auto drag = [&](ImVec2 from, ImVec2 to, int button = 0)
    {
        ImGui::GetIO().AddMousePosEvent(from.x, from.y);
        render();
        ImGui::GetIO().AddMouseButtonEvent(button, true);
        render();
        ImGui::GetIO().AddMousePosEvent(to.x, to.y);
        render();
        render();
        ImGui::GetIO().AddMouseButtonEvent(button, false);
        render();
        render();
    };
    drag(port("scene.hdr"), port("gain.source", true));
    require(document.definition.edges.size() == 1 && document.definition.edges.front().from == "scene.hdr" &&
                document.definition.edges.front().to == "gain.source",
            "Dragging pins did not create the graph connection");
    drag(port("scene.depth"), port("gain.source", true));
    require(document.definition.edges.size() == 1 && document.definition.edges.front().from == "scene.hdr",
            "Incompatible depth/HDR ports replaced a valid wire");
    drag(port("scene.position_metallic"), port("gain.source", true));
    require(document.definition.edges.size() == 1 && document.definition.edges.front().from == "scene.hdr",
            "Incompatible full-precision position/HDR ports replaced a valid wire");
    drag(port("gain.color"), port("second.source", true));
    require(document.definition.edges.size() == 2, "Chaining project passes did not create a wire");
    drag(port("scene.hdr"), port("second.source", true));
    require(document.definition.edges.size() == 2 && std::ranges::any_of(document.definition.edges,
                                                                         [](const auto& edge)
                                                                         {
                                                                             return edge.to == "second.source" &&
                                                                                    edge.from == "scene.hdr";
                                                                         }),
            "Reconnecting an input did not replace its old wire");
    drag(port("gain.color"), port("display.hdr", true));
    require(document.definition.outputs.front() == "gain.color", "Display wire did not select the primary output");

    const auto beforePan       = port("gain.source", true);
    const auto layoutBeforePan = document.nodePositions;
    drag({1200, 640}, {1100, 640}, 2);
    const auto afterPan = port("gain.source", true);
    require(std::abs(afterPan.x - beforePan.x) > 50 && document.nodePositions == layoutBeforePan,
            "Canvas pan moved graph nodes or failed to move the view");
    const auto from     = port("scene.hdr");
    const auto to       = port("gain.source", true);
    const auto distance = to.x - from.x;
    ImGui::GetIO().AddMousePosEvent(650, 550);
    ImGui::GetIO().AddMouseWheelEvent(0, -1);
    for (int i = 0; i < 15; ++i)
    {
        render();
    }
    require(std::abs((port("gain.source", true).x - port("scene.hdr").x) - distance) > 1,
            "Mouse wheel did not zoom the node canvas");

    const auto input = port("second.source", true);
    drag({input.x + 85, input.y - 48}, {input.x + 125, input.y + 40});
    const auto movement = document.nodePositions.at("pass:second") - layoutBeforePan.at("pass:second");
    require(std::abs(movement.x) + std::abs(movement.y) > 25, "Dragging a node did not change its saved layout");
    const auto click = [&](ImVec2 point, int button = 0)
    {
        ImGui::GetIO().AddMousePosEvent(point.x, point.y);
        render();
        ImGui::GetIO().AddMouseButtonEvent(button, true);
        render();
        ImGui::GetIO().AddMouseButtonEvent(button, false);
        render();
    };
    const auto output = port("second.color");
    click(output);
    click(output);
    require(preview == "second.color" &&
                std::ranges::find(document.definition.outputs, "second.color") != document.definition.outputs.end(),
            "Double-clicking an output did not mark/select it for preview");

    const auto source      = port("scene.hdr");
    const auto destination = port("gain.source", true);
    click({(source.x + destination.x) * 0.5f, (source.y + destination.y) * 0.5f}, 1);
    render();
    require(document.definition.edges.size() == 1 && document.definition.edges.front().to == "second.source",
            "Right-clicking a wire did not disconnect it");
    drag(port("scene.hdr"), port("gain.source", true));

    const auto   gainInput  = port("gain.source", true);
    const auto   gainOutput = port("gain.color");
    const auto   zoom       = (gainOutput.x - gainInput.x) / 224;
    const ImVec2 slider {gainInput.x + 170 * zoom, (gainInput.y + gainOutput.y) * 0.5f};
    document.size = {129, 97};
    workspace     = std::make_unique<ResearchWorkspace>(device, server, catalog);
    workspace->replace(document);
    render();
    auto*      activeGraph   = &workspace->graph();
    auto*      passInstance  = activeGraph->instances.passes.front().instance.get();
    auto*      outputTexture = activeGraph->graph.getTexture(activeGraph->rendererOutputs.color).handle;
    const auto image         = [&]
    {
        return readback(device, activeGraph->graph.getTexture(activeGraph->rendererOutputs.color));
    };
    const auto beforeDrag           = image();
    const auto changesBeforeDrag    = changes;
    const auto parametersBeforeDrag = parameterChanges;
    ImGui::GetIO().AddMousePosEvent(slider.x, slider.y);
    render();
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    render();
    const auto heldImage = image();
    const auto heldGain  = document.definition.passes.front().parameters.at("gain");
    require(ImGui::GetIO().MouseDown[0] && parameterChanges > parametersBeforeDrag && heldGain != 1 &&
                heldImage.rgba != beforeDrag.rgba,
            "Slider did not preview the rendered image while the mouse was held");
    ImGui::GetIO().AddMousePosEvent(slider.x - 30 * zoom, slider.y);
    render();
    require(ImGui::GetIO().MouseDown[0] && document.definition.passes.front().parameters.at("gain") != heldGain &&
                image().rgba != heldImage.rgba,
            "Dragging an active slider did not continue updating the rendered image");
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    render();
    require(changes == changesBeforeDrag && &workspace->graph() == activeGraph &&
                activeGraph->instances.passes.front().instance.get() == passInstance &&
                activeGraph->graph.getTexture(activeGraph->rendererOutputs.color).handle == outputTexture,
            "Parameter drag or release requested graph compilation or recreated GPU state");
    workspace.reset();

    const auto deleteInput = port("second.source", true);
    click({deleteInput.x + 85, deleteInput.y - 48});
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, true);
    render();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, false);
    render();
    require(document.definition.passes.size() == 1 && document.definition.edges.size() == 1 &&
                std::ranges::find(document.definition.outputs, "second.color") == document.definition.outputs.end() &&
                !document.nodePositions.contains("pass:second"),
            "Deleting a node did not remove its wires, marked output and layout");
    const auto renameInput = port("gain.source", true);
    click({renameInput.x + 85, renameInput.y - 48}, 1);
    render();
    render();
    auto& popupStack = ImGui::GetCurrentContext()->OpenPopupStack;
    require(!popupStack.empty() && popupStack.back().Window, "Pass context menu did not open");
    const auto* popup         = popupStack.back().Window;
    const auto  popupPosition = popup->Pos;
    const auto  popupSize     = popup->Size;
    click({popupPosition.x + popupSize.x * 0.75f, popupPosition.y + 18});
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_A, true);
    render();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_A, false);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    ImGui::GetIO().AddInputCharactersUTF8("tint");
    render();
    click({popupPosition.x + 32, popupPosition.y + 40});
    require(document.definition.passes.front().id == "tint" && document.definition.edges.front().to == "tint.source" &&
                document.definition.outputs.front() == "tint.color" && document.nodePositions.contains("pass:tint") &&
                !document.nodePositions.contains("pass:gain"),
            "Instance rename lost connections, outputs or layout");

    const auto toolbar = ImGui::FindWindowByName("Graph interaction")->DC.CursorStartPos;
    click({toolbar.x + 120, toolbar.y + 9});
    render();
    require(!popupStack.empty() && popupStack.back().Window, "Pass type selector did not open");
    const auto* selector    = popupStack.back().Window;
    const auto  definitions = catalog.definitions();
    const auto  selected    = std::ranges::find(definitions, type, &PassDefinition::type);
    require(selected != definitions.end(), "Expected project pass is absent from the catalog");
    const auto row = float(selected - definitions.begin());
    click({selector->DC.CursorStartPos.x + 80,
           selector->DC.CursorStartPos.y + row * ImGui::GetTextLineHeightWithSpacing() + 8});
    click({toolbar.x + 270, toolbar.y + 9});
    require(document.definition.passes.size() == 2 && document.definition.passes.back().type == type,
            "Add pass toolbar did not create the selected pass type");
    const auto addedId    = document.definition.passes.back().id;
    const auto addedInput = port(addedId + ".source", true);
    click({addedInput.x + 85, addedInput.y - 48}, 1);
    render();
    render();
    require(!popupStack.empty() && popupStack.back().Window, "New pass context menu did not open");
    const auto deletePopup = popupStack.back().Window->Pos;
    click({deletePopup.x + 120, deletePopup.y + 40});
    render();
    render();
    require(document.definition.passes.size() == 1, "Delete pass context menu did not remove the node");
    click({toolbar.x + 270, toolbar.y + 9});
    require(document.definition.passes.size() == 2 && document.definition.passes.back().id == addedId &&
                document.nodePositions.contains("pass:" + addedId) && editor.portPosition(addedId + ".source", true),
            "Recreating a deleted instance lost its node identity or ports");
    editor.saveLayout(document);
    const auto directory = std::filesystem::path("build/.tmp") / ("graph-editor-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    const auto file = directory / "layout.vworkspace";
    document.save(file);
    const auto reopened = ResearchDocument::load(file);
    require(reopened.nodePositions == document.nodePositions &&
                reopened.definition.serialize() == document.definition.serialize(),
            "Workspace lost node positions or graph topology");
    require(changes >= 4, "Canvas did not request graph compilation after pointer edits");
    savePng(readback(device, target), directory / "canvas.png");
    std::cout << "Graph canvas tests passed: wiring, incompatible ports, rewiring, display, pan, zoom, node drag, "
                 "preview, live parameter GPU output while dragging, delete, rename, add and save\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
