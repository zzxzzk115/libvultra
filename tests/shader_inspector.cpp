#include <vultra/assets/shader_asset.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/ui/editor_gui.hpp>
#include <vultra/ui/editor_gui_shader_material.hpp>

#include <imgui_internal.h>

#include <cstdio>
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
    const auto asset = ShaderAsset::parse("inspector.vshader", R"shader(Shader "Tests/Inspector"
    {
        Properties
        {
            [HideInInspector] gain ("Hidden Gain", Float) = 2
            enabled ("Enabled", Boolean) = false
        }
        Variant "Default" {}
        Variant "Alternate" {}
        SubShader { Pass { Name "Compute" Compute main SLANGPROGRAM
[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {}
ENDSLANG
        } }
    })shader");
    Device     device;
    EditorGui  gui(device, VriFormat_RGBA8_UNORM, {.multiViewport = false, .persistLayout = false});
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    constexpr Extent size {500, 240};
    Texture          target(device, colorTexture(size));
    Frame            frame(device);
    MaterialInstance instance;
    ImVec2           checkboxPoint;
    ImVec2           resetPoint;
    ImVec2           variantPoint;
    const auto       render = [&]
    {
        gui.begin(size, 1.0f / 60);
        auto ui = gui.frame();
        ui.setNextWindowPos({12, 12}, ImGuiCond_Always);
        ui.setNextWindowSize({476, 216}, ImGuiCond_Always);
        {
            EditorGuiWindow window(ui, "Shader material");
            const auto      cursor = ImGui::GetCursorScreenPos();
            variantPoint           = {cursor.x + 12, cursor.y + ImGui::GetFrameHeight() * 0.5f};
            ImGui::PushID(&instance);
            const auto tableId = ImGui::GetID("shader-properties");
            ImGui::PopID();
            drawShaderMaterialInspector(gui, asset, instance);
            // Read actual Inspector table geometry; pointer events still activate the production widgets.
            const auto* table = ImGui::TableFindByID(tableId);
            require(table != nullptr, "Shader Inspector did not create its property table");
            const auto height = ImGui::GetFrameHeight();
            const auto left   = table->Columns[1].WorkMinX;
            checkboxPoint     = {left + height * 0.5f, table->RowPosY1 + height * 0.5f};
            resetPoint = {left + height + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Reset").x * 0.5f +
                              ImGui::GetStyle().FramePadding.x,
                          checkboxPoint.y};
        }
        gui.upload(size);
        auto* commands = frame.begin();
        gui.copy(commands);
        target.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const std::array<float, 4> clear {0, 0, 0, 1};
        beginColorPass(device, commands, target.view(), size, clear.data());
        gui.draw(commands);
        device.core.CmdEndRendering(commands);
        frame.submitAndWait();
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
    render();
    render();
    const auto before   = readback(device, target);
    const auto revision = instance.revision();
    click(checkboxPoint);
    require(std::get<bool>(instance.value(asset, "enabled")) && instance.revision() == revision + 1,
            "Inspector Boolean pointer edit did not reach the material");
    require(compare(before, readback(device, target)).mse > 0, "Inspector edit was not rendered");
    click(resetPoint);
    require(!std::get<bool>(instance.value(asset, "enabled")), "Inspector Reset did not restore the shader default");
    require(std::get<float>(instance.value(asset, "gain")) == 2, "Hidden property was changed by the Inspector");
    click(variantPoint);
    const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
    require(!popups.empty() && popups.back().Window, "Inspector variant selector did not open");
    const auto popup = popups.back().Window->WorkRect.Min;
    click({popup.x + 20, popup.y + ImGui::GetTextLineHeightWithSpacing() * 1.5f});
    require(instance.variant == "Alternate", "Inspector variant selection did not reach the instance");
    render();
    savePng(readback(device, target), "build/.tmp/shader-checks/inspector.png");
    std::puts("Shader Inspector passed: real GUI edits, reset, hidden properties, variant selection and GPU drawing");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
