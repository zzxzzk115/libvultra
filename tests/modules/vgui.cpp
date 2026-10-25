#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/image/image.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/ui/vgui.hpp>

#include <array>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace vultra;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void writeText(const std::filesystem::path& file, std::string_view text)
    {
        writeFileAtomically(file, std::as_bytes(std::span(text)));
    }

    Image render(Device& device, AssetSource& source, const AssetSource* shaders)
    {
        const Extent extent {320, 200};
        Texture      target(device, colorTexture(extent));
        Frame        frame(device);
        VGui         gui(device, extent, VriFormat_RGBA8_UNORM, &source, shaders);
        gui.loadFont("ui/font.ttf");
        gui.loadDocument("ui/main.rml");
        Input input;
        int   changes = 0;
        gui.bindChange("toggle",
                       [&]
                       {
                           ++changes;
                       });
        const auto draw = [&]
        {
            input.advanceFrame();
            gui.processInput(input, extent);
            gui.update(extent);
            auto* cmd = frame.begin();
            target.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[] {0, 0, 0, 1};
            beginColorPass(device, cmd, target.view(), extent, clear);
            device.core.CmdEndRendering(cmd);
            gui.draw(cmd, target);
            frame.submitAndWait();
        };
        draw();
        draw();
        const auto baseline = readback(device, target);
        const auto sprite   = imagePixel(baseline, 20, 20);
        const auto patch    = imagePixel(baseline, 90, 20);
        require(sprite[0] > 0.9f && sprite[1] < 0.1f && sprite[2] > 0.9f, "VGui did not render its PNG resource");
        require(patch[0] < 0.1f && patch[1] > 0.9f, "VGui did not read its relative stylesheet");
        input.setMousePosition({22, 88});
        input.setMouseButton(MouseCode::eLeft, true);
        draw();
        input.setMouseButton(MouseCode::eLeft, false);
        draw();
        require(gui.isChecked("toggle") && changes == 1, "VGui checkbox lost input or its callback");
        gui.setChecked("toggle", false);
        require(changes == 2 && !gui.isChecked("toggle"), "VGui programmatic edit lost its callback");
        input.setMousePosition({-1, -1});
        gui.loadDocument("ui/main.rml");
        draw();
        draw();
        require(readback(device, target).rgba == baseline.rgba, "VGui document reload changed its pixels");
        int replacements = 0;
        gui.bindChange("toggle",
                       [&]
                       {
                           ++replacements;
                           gui.loadDocument("ui/main.rml");
                       });
        gui.setChecked("toggle", true);
        require(replacements == 1 && !gui.isChecked("toggle"), "Callback could not safely replace its document");
        gui.setChecked("toggle", true);
        require(replacements == 1, "Removed document callback was invoked again");
        return baseline;
    }
} // namespace

int main(int argc, char** argv)
try
{
    require(argc == 2 || argc == 3, "Pass an unused output directory and optional cooked builtin.vpk");
    std::optional<AssetSource> shaders;
    if (argc == 3)
    {
        shaders.emplace(VpkArchive(argv[2]));
    }
    const std::filesystem::path directory(argv[1]);
    require(!std::filesystem::exists(directory), "VGui output directory already exists");
    const auto root = directory / "source";
    std::filesystem::create_directories(root / "ui/styles");
    std::filesystem::create_directories(root / "textures");
    std::filesystem::copy_file("resources/ui/LatoLatin-Regular.ttf", root / "ui/font.ttf");
    Image sprite {{16, 16}, std::vector<float>(16 * 16 * 4)};
    for (size_t index = 0; index < sprite.rgba.size(); index += 4)
    {
        sprite.rgba[index]     = 1;
        sprite.rgba[index + 2] = 1;
        sprite.rgba[index + 3] = 1;
    }
    savePng(sprite, root / "textures/ui.png");
    writeText(root / "ui/main.rml", R"(<rml><head>
<link type="text/rcss" href="styles/main.rcss" /></head><body>
<img id="sprite" src="../textures/ui.png"/><div id="patch"></div>
<input id="toggle" type="checkbox"/><div id="text">Independent game UI</div>
</body></rml>)");
    writeText(root / "ui/styles/main.rcss", R"(body { font-family: LatoLatin; color: #fff; }
#sprite { position: absolute; left: 10px; top: 10px; width: 40px; height: 40px; }
#patch { position: absolute; left: 80px; top: 10px; width: 40px; height: 40px; background-color: #00ff00; }
#toggle { position: absolute; left: 10px; top: 75px; }
#text { position: absolute; left: 10px; top: 125px; font-size: 20px; })");
    const std::array files {VpkFile {"ui/main.rml", root / "ui/main.rml"},
                            VpkFile {"ui/styles/main.rcss", root / "ui/styles/main.rcss"},
                            VpkFile {"ui/font.ttf", root / "ui/font.ttf"},
                            VpkFile {"textures/ui.png", root / "textures/ui.png"}};
    const auto       package = directory / "ui.vpk";
    VpkArchive::pack(files, package);
    Device      device;
    AssetSource authored(root);
    const auto  baseline = render(device, authored, shaders ? &*shaders : nullptr);
    savePng(baseline, directory / "filesystem.png");
    std::filesystem::remove_all(root);
    AssetSource packaged {VpkArchive(package)};
    const auto  result = render(device, packaged, shaders ? &*shaders : nullptr);
    require(result.rgba == baseline.rgba, "Filesystem and VPK VGui output differ");
    savePng(result, directory / "packaged.png");
    std::cout << "Independent VGui passed: texture, stylesheet, checkbox input and filesystem/VPK parity\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
