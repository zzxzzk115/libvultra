#include <vultra/api/ui_bridge.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <array>
#include <iostream>
#include <limits>
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

    ImVec2 itemCenter()
    {
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    }
} // namespace

int main()
try
{
    using namespace vultra;
    Device               device;
    EditorGui            gui(device,
                  static_cast<Window*>(nullptr),
                  VriFormat_RGBA8_UNORM,
                             {.multiViewport = false, .persistLayout = false});
    const Extent         extent {480, 360};
    Texture              target(device, colorTexture(extent));
    Frame                frame(device);
    bool                 enabled  = false;
    std::string          text     = "short";
    int                  selected = 0;
    double               gain     = 1;
    PropertySchema       thresholdSchema {.name         = "threshold",
                                          .label        = "Threshold",
                                          .drawerId     = "threshold",
                                          .jsonPath     = "/threshold",
                                          .kind         = PropertyKind::eFloat,
                                          .speed        = 0.01f,
                                          .defaultValue = 0.5f};
    const auto           schema                    = parsePropertySchema(serializePropertySchema(thresholdSchema));
    PropertyValue        threshold                 = schema.defaultValue;
    bool                 thresholdChangedWhileHeld = false;
    std::array<float, 3> vector {};
    std::string          propertyText = "Property";
    float                fontSize     = 0;
    ImVec2               floatPosition;
    ImVec2               vectorPosition;
    ImVec2               propertyTextPosition;
    ImVec2               checkboxPosition;
    ImVec2               textPosition;
    ImVec2               comboPosition;
    ImVec2               secondPosition;
    const auto           render = [&]
    {
        gui.begin(extent, 1.0f / 60);
        auto ui = gui.frame();
        ui.setNextWindowPos({20, 20}, ImGuiCond_Always);
        ui.setNextWindowSize({420, 290}, ImGuiCond_Always);
        {
            EditorGuiWindow window(ui, "Offscreen controls");
            require(bool(window), "Offscreen GUI window is not visible");
            fontSize           = ImGui::GetFontSize();
            uint8_t abiEnabled = enabled;
            uint8_t changed    = 0;
            require(uiApi().checkbox(makeUiFrame(gui), "Enabled", 7, &abiEnabled, &changed) == VULTRA_STATUS_OK,
                    "Generated checkbox ABI failed in a live frame");
            enabled          = abiEnabled != 0;
            checkboxPosition = itemCenter();
            ui.inputText("Project path", &text);
            textPosition = itemCenter();
            ui.sliderDouble("Gain", &gain, 0, 8);
            if (ui.beginCombo("Output", selected == 0 ? "first" : "second"))
            {
                if (ui.selectable("first", selected == 0))
                {
                    selected = 0;
                }
                if (ui.selectable("second", selected == 1))
                {
                    selected = 1;
                }
                secondPosition = itemCenter();
                ui.endCombo();
            }
            else
            {
                comboPosition = itemCenter();
            }
            EditorGuiInspector inspector(gui, "typed-properties");
            if (inspector)
            {
                inspector.valueField(schema, threshold);
                thresholdChangedWhileHeld |=
                    ImGui::IsItemActive() && ImGui::GetIO().MouseDown[0] && std::get<float>(threshold) > 0.5f;
                floatPosition = itemCenter();
                inspector.float3Field({"vector", "Position"}, vector.data());
                const auto min = ImGui::GetItemRectMin();
                const auto max = ImGui::GetItemRectMax();
                vectorPosition = {min.x + (max.x - min.x) / 6, (min.y + max.y) * 0.5f};
                inspector.textField({"text", "Name"}, &propertyText);
                propertyTextPosition = itemCenter();
            }
        }
        gui.upload(extent);
        auto* cmd = frame.begin();
        gui.copy(cmd);
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 0, 1};
        beginColorPass(device, cmd, target.view(), extent, clear);
        gui.draw(cmd);
        device.core.CmdEndRendering(cmd);
        frame.submitAndWait();
    };
    bool rejected = false;
    try
    {
        gui.begin(extent, std::numeric_limits<float>::quiet_NaN());
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "Offscreen frame accepted a non-finite time step");
    render();
    render();
    const auto click = [&](ImVec2 point)
    {
        ImGui::GetIO().AddMousePosEvent(point.x, point.y);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        render();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        render();
    };
    click(checkboxPosition);
    require(enabled, "Offscreen checkbox did not receive mouse input");
    click(textPosition);
    const std::string appended(512, 'x');
    ImGui::GetIO().AddInputCharactersUTF8(appended.c_str());
    render();
    require(text.size() == 517, "Dynamic text input lost data or failed to resize its string");
    click(comboPosition);
    render(); // Let ImGui finish the popup's initial placement before using its item coordinates.
    require(secondPosition.x > 0 && secondPosition.y > 0, "Offscreen dropdown did not open");
    click(secondPosition);
    require(selected == 1, "Offscreen dropdown selection did not receive mouse input");
    const auto drag = [&](ImVec2 point)
    {
        ImGui::GetIO().AddMousePosEvent(point.x, point.y);
        render();
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        render();
        ImGui::GetIO().AddMousePosEvent(point.x + 40, point.y);
        render();
        render();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        render();
    };
    drag(floatPosition);
    drag(vectorPosition);
    require(thresholdChangedWhileHeld && std::get<float>(threshold) > 0.5f && vector[0] > 0 && vector[1] == 0 &&
                vector[2] == 0,
            "Inspector scalar/vector drag did not update the intended values");
    click(propertyTextPosition);
    ImGui::GetIO().AddInputCharactersUTF8(appended.c_str());
    render();
    require(propertyText.size() == 520, "Inspector text field did not resize its borrowed string");
    const auto image = readback(device, target);
    bool       hasUi = false;
    for (size_t i = 0; i < image.rgba.size(); i += 4)
    {
        hasUi |= image.rgba[i] > 0.5f;
    }
    require(hasUi && gain == 1, "Offscreen GPU output lost the widgets or changed an unedited value");
    savePng(image, std::filesystem::path("build/.tmp") / ("gui-offscreen-" + StableId::generate().toString() + ".png"));
    // Exercise atlas updates at new font sizes and switching back, with real GPU draws/readback.
    const auto baseFontSize = fontSize;
    for (const auto scale : {2.0f, 1.0f})
    {
        ImGui::GetStyle().FontScaleMain = scale;
        render();
        render();
        const auto scaledImage = readback(device, target);
        require(fontSize == baseFontSize * scale, "Font size did not follow UI scale");
        size_t textPixels = 0;
        for (size_t pixel = 0; pixel < scaledImage.rgba.size(); pixel += 4)
        {
            textPixels += scaledImage.rgba[pixel] > 0.5f;
        }
        require(textPixels > 100, "Resized font atlas lost visible text");
        savePng(scaledImage,
                std::filesystem::path("build/.tmp") /
                    ("gui-font-" + std::to_string(int(scale)) + "x-" + StableId::generate().toString() + ".png"));
    }
    // A narrow inspector must keep long labels visible when the font atlas grows for HiDPI.
    ImGui::GetStyle().FontScaleMain = 2.5f;
    for (int pass = 0; pass < 2; ++pass)
    {
        gui.begin(extent, 1.0f / 60);
        ImGui::SetNextWindowPos({20, 20}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({420, 290}, ImGuiCond_Always);
        ImGui::Begin("HiDPI property");
        const auto rowStart = ImGui::GetCursorPosY();
        auto       ui       = gui.frame();
        ui.checkbox("Environment intensity", &enabled);
        require(ImGui::GetCursorPosY() - rowStart > ImGui::GetTextLineHeight() * 1.5f,
                "Long HiDPI property label was clipped instead of wrapping");
        ImGui::End();
        gui.upload(extent);
        auto* cmd = frame.begin();
        gui.copy(cmd);
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 0, 1};
        beginColorPass(device, cmd, target.view(), extent, clear);
        gui.draw(cmd);
        device.core.CmdEndRendering(cmd);
        frame.submitAndWait();
    }
    savePng(readback(device, target),
            std::filesystem::path("build/.tmp") / ("gui-property-250-" + StableId::generate().toString() + ".png"));
    std::cout
        << "Offscreen GUI tests passed: mouse, resized text, property scalar/vector/text, dropdown and GPU image\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
