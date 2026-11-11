#include "support/check.hpp"

#include <vultra/gpu/image_readback.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <iostream>
#include <utility>

namespace
{
    using support::must;
    using support::require;

    // Widgets of an expired or wrong-context frame abort, so the test checks the detection they rely on.
    constexpr std::string_view kAccepted = "Expired or wrong-context GUI frame was accepted";
} // namespace

int main()
{
    using namespace vultra;
    const auto device = must(Device::create());
    const auto first =
        must(EditorGui::create(*device, VriFormat_RGBA8_UNORM, {.multiViewport = false, .persistLayout = false}));
    const Extent size {320, 200};
    auto         target     = must(Texture::create(*device, colorTexture(size)));
    const auto   submission = must(Frame::create(*device));
    {
        const auto second =
            must(EditorGui::create(*device, VriFormat_RGBA8_UNORM, {.multiViewport = false, .persistLayout = false}));
        must(first->begin(size, 1.0f / 60));
        auto  oldFrame     = first->frame();
        auto* firstContext = ImGui::GetCurrentContext();
        must(second->begin(size, 1.0f / 60));
        require(ImGui::GetCurrentContext() != firstContext, "GUI instances share an ImGui context");
        support::rejects(oldFrame.requireActive(), kAccepted);
        must(second->upload(size));
        must(first->upload(size));
        support::rejects(oldFrame.requireActive(), kAccepted);
        must(first->begin(size, 1.0f / 60));
        support::rejects(oldFrame.requireActive(), kAccepted);
        auto ui = first->frame();
        must(ui.requireActive());
        ui.setNextWindowPos({10, 10}, ImGuiCond_Always);
        ui.setNextWindowSize({290, 170}, ImGuiCond_Always);
        {
            EditorGuiWindow window(ui, "Independent context");
            int             calls = 0;
            std::pair       state {first.get(), &calls};
            must(first->setPropertyDrawer("self-removing",
                                          {[](EditorGuiProperty, void*, void* data)
                                           {
                                               auto& state = *static_cast<std::pair<EditorGui*, int*>*>(data);
                                               ++*state.second;
                                               state.first->removePropertyDrawer("self-removing");
                                               return true;
                                           },
                                           &state}));
            bool               value = false;
            EditorGuiInspector inspector(*first, "properties");
            require(inspector.boolField({"self-removing", "Enabled"}, &value) && calls == 1,
                    "Self-removing property drawer did not finish its call");
            require(!first->propertyDrawer("self-removing") && !second->propertyDrawer("self-removing"),
                    "Drawer registration escaped its owning GUI context");
        }
        must(first->upload(size));
        auto* commands = must(submission->begin());
        first->copy(commands);
        target.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 0, 1};
        beginColorPass(*device, commands, must(target.view()), size, clear);
        first->draw(commands);
        device->core.CmdEndRendering(commands);
        must(submission->submitAndWait());
        const auto image = must(readback(*device, target));
        require(must(imagePixel(image, 30, 30))[0] > 0.01f, "Independent GUI context did not render");
    }
    must(first->begin(size, 1.0f / 60));
    auto ui = first->frame();
    ui.setNextWindowSize({100, 100}, ImGuiCond_Always);
    {
        EditorGuiWindow window(ui, "Surviving context");
        ui.textUnformatted("Still alive");
    }
    must(first->upload(size));
    std::cout << "GUI context isolation, expired frames, drawer revocation and destruction passed\n";
    return 0;
}
