#include <vultra/api/ui_bridge.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <iostream>
#include <stdexcept>
#include <utility>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template<class Function>
    void reject(Function&& function)
    {
        bool rejected = false;
        try
        {
            function();
        }
        catch (const std::logic_error&)
        {
            rejected = true;
        }
        require(rejected, "Expired or wrong-context GUI frame was accepted");
    }
} // namespace

int main()
try
{
    using namespace vultra;
    require(uiApi().fingerprint == VULTRA_UI_API_FINGERPRINT, "Generated UI ABI fingerprint differs from its table");
    Device       device;
    EditorGui    first(device, VriFormat_RGBA8_UNORM, {.multiViewport = false, .persistLayout = false});
    const Extent size {320, 200};
    Texture      target(device, colorTexture(size));
    Frame        submission(device);
    {
        EditorGui second(device, VriFormat_RGBA8_UNORM, {.multiViewport = false, .persistLayout = false});
        first.begin(size, 1.0f / 60);
        auto       oldFrame     = first.frame();
        const auto borrowed     = makeUiFrame(first);
        auto*      firstContext = ImGui::GetCurrentContext();
        second.begin(size, 1.0f / 60);
        require(ImGui::GetCurrentContext() != firstContext, "GUI instances share an ImGui context");
        reject(
            [&]
            {
                oldFrame.textUnformatted("Wrong context");
            });
        uint8_t abiValue   = 0;
        uint8_t abiChanged = 0;
        require(uiApi().checkbox(borrowed, "Wrong", 5, &abiValue, &abiChanged) == VULTRA_STATUS_INVALID_FRAME,
                "Generated UI ABI accepted a frame from another current context");
        second.upload(size);
        first.upload(size);
        reject(
            [&]
            {
                oldFrame.button("Expired frame");
            });
        first.begin(size, 1.0f / 60);
        reject(
            [&]
            {
                oldFrame.separator();
            });
        auto ui = first.frame();
        ui.setNextWindowPos({10, 10}, ImGuiCond_Always);
        ui.setNextWindowSize({290, 170}, ImGuiCond_Always);
        {
            EditorGuiWindow window(ui, "Independent context");
            int             calls = 0;
            std::pair       state {&first, &calls};
            first.setPropertyDrawer("self-removing",
                                    {[](EditorGuiProperty, void*, void* data)
                                     {
                                         auto& state = *static_cast<std::pair<EditorGui*, int*>*>(data);
                                         ++*state.second;
                                         state.first->removePropertyDrawer("self-removing");
                                         return true;
                                     },
                                     &state});
            bool               value = false;
            EditorGuiInspector inspector(first, "properties");
            require(inspector.boolField({"self-removing", "Enabled"}, &value) && calls == 1,
                    "Self-removing property drawer did not finish its call");
            require(!first.propertyDrawer("self-removing") && !second.propertyDrawer("self-removing"),
                    "Drawer registration escaped its owning GUI context");
        }
        first.upload(size);
        auto* commands = submission.begin();
        first.copy(commands);
        target.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 0, 1};
        beginColorPass(device, commands, target.view(), size, clear);
        first.draw(commands);
        device.core.CmdEndRendering(commands);
        submission.submitAndWait();
        const auto image = readback(device, target);
        require(imagePixel(image, 30, 30)[0] > 0.01f, "Independent GUI context did not render");
    }
    first.begin(size, 1.0f / 60);
    auto ui = first.frame();
    ui.setNextWindowSize({100, 100}, ImGuiCond_Always);
    {
        EditorGuiWindow window(ui, "Surviving context");
        ui.textUnformatted("Still alive");
    }
    first.upload(size);
    std::cout << "GUI context isolation, expired frames, drawer revocation and destruction passed\n";
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
