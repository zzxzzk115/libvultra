#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/main/experiment_session.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/ui/vgui.hpp>

#include <RmlUi/Core.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace vultra;
    namespace fs = std::filesystem;

    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    template<typename Callback>
    void requireFailure(Callback&& callback, const char* message)
    {
        bool failed = false;
        try
        {
            callback();
        }
        catch (const std::exception&)
        {
            failed = true;
        }
        require(failed, message);
    }

    Image renderUi(Device& device, const AssetSource* source, const fs::path& document, const fs::path& font)
    {
        const Extent size {320, 200};
        Texture      target(device, colorTexture(size));
        Frame        frame(device);
        VGui         gui(device, size, VriFormat_RGBA8_UNORM, source);
        gui.loadFont(font);
        gui.loadDocument(document);
        Input input;
        int   changes = 0;
        gui.bindChange("toggle",
                       [&]
                       {
                           ++changes;
                       });
        const auto render = [&]
        {
            input.advanceFrame();
            gui.update(input, size);
            auto* cmd = frame.begin();
            target.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[4] {0, 0, 0, 1};
            beginColorPass(device, cmd, target.view(), size, clear);
            device.core.CmdEndRendering(cmd);
            gui.draw(cmd, target);
            frame.submitAndWait();
        };
        render();
        render();
        const auto baseline = readback(device, target);
        const auto color    = [&](uint32_t x, uint32_t y, uint32_t channel)
        {
            return baseline.rgba[(y * size.width + x) * 4 + channel];
        };
        require(color(20, 20, 0) > 0.9f && color(20, 20, 2) > 0.9f && color(20, 20, 1) < 0.1f,
                "PNG resource did not reach VGUI GPU output");
        require(color(90, 20, 1) > 0.9f && color(90, 20, 0) < 0.1f,
                "Nested relative stylesheet did not reach VGUI GPU output");
        size_t glyphPixels = 0;
        for (uint32_t y = 125; y < 165; ++y)
        {
            for (uint32_t x = 10; x < 190; ++x)
            {
                glyphPixels += color(x, y, 0) > 0.8f && color(x, y, 1) > 0.8f && color(x, y, 2) > 0.8f;
            }
        }
        require(glyphPixels > 100, "Font resource did not render text");
        input.setMousePosition({22, 88});
        input.setMouseButton(MouseCode::eLeft, true);
        render();
        input.setMouseButton(MouseCode::eLeft, false);
        render();
        require(gui.isChecked("toggle") && changes == 1, "Offscreen VGUI checkbox lost its change callback");
        requireFailure(
            [&]
            {
                gui.loadDocument(document.parent_path() / "broken.rml");
            },
            "VGUI silently accepted a missing stylesheet");
        require(gui.isChecked("toggle"), "Failed document replacement discarded the active UI");
        gui.setChecked("toggle", false);
        require(!gui.isChecked("toggle") && changes == 2, "Programmatic checkbox edit lost its change callback");
        render();
        input.setMouseButton(MouseCode::eLeft, true);
        render();
        input.setMouseButton(MouseCode::eLeft, false);
        render();
        require(gui.isChecked("toggle") && changes == 3, "Failed replacement discarded listeners");
        // Exercise RmlUi's seekable font/document stream, including signed bounds on LLP64 hosts.
        auto*      files  = Rml::GetFileInterface();
        const auto path   = source ? source->resolve(font) : font;
        const auto text   = path.generic_u8string();
        const auto handle = files->Open({text.begin(), text.end()});
        require(handle != 0, "Could not reopen font stream");
        const auto length = files->Length(handle);
        require(length > 4 && files->Seek(handle, -4, SEEK_END) && files->Tell(handle) == length - 4 &&
                    !files->Seek(handle, LONG_MIN, SEEK_CUR) && !files->Seek(handle, 5, SEEK_CUR) &&
                    files->Tell(handle) == length - 4,
                "VGUI stream seek escaped its resource bounds");
        files->Close(handle);
        input.setMousePosition({-1, -1});
        gui.loadDocument(document);
        render();
        render();
        const auto recovered = readback(device, target);
        require(recovered.rgba == baseline.rgba, "Document recovery changed unedited UI pixels");
        return recovered;
    }
} // namespace

int main()
try
{
    const auto run  = fs::absolute(fs::path("build/.tmp") / ("vgui-" + StableId::generate().toString()));
    const auto root = run / "project";
    fs::create_directories(root / "ui/styles");
    fs::create_directories(root / "textures");
    fs::copy_file("resources/ui/LatoLatin-Regular.ttf", root / "ui/font.ttf");
    Image sprite {{16, 16}, std::vector<float>(16 * 16 * 4)};
    for (size_t i = 0; i < sprite.rgba.size(); i += 4)
    {
        sprite.rgba[i]     = 1;
        sprite.rgba[i + 2] = 1;
        sprite.rgba[i + 3] = 1;
    }
    savePng(sprite, root / "textures/ui.png");
    std::ofstream(root / "ui/main.rml") << R"(<rml><head>
<link type="text/rcss" href="styles/main.rcss" />
<link type="text/rcss" href="styles/colors.rcss" /></head><body>
<img id="sprite" src="../textures/ui.png"/><div id="patch"></div>
<input id="toggle" type="checkbox"/><div id="text">Packaged UI</div>
</body></rml>)";
    std::ofstream(root / "ui/styles/main.rcss") << R"(body { font-family: LatoLatin; color: #fff; }
#sprite { position: absolute; left: 10px; top: 10px; width: 40px; height: 40px; }
#patch { position: absolute; left: 80px; top: 10px; width: 40px; height: 40px; }
#toggle { position: absolute; left: 10px; top: 75px; }
#text { position: absolute; left: 10px; top: 125px; font-size: 20px; })";
    std::ofstream(root / "ui/styles/colors.rcss") << "#patch { background-color: #00ff00; }";
    std::ofstream(root / "ui/broken.rml") << R"(<rml><head>
<link type="text/rcss" href="styles/missing.rcss" /></head><body></body></rml>)";
    ProjectManifest project;
    project.mainScene  = "main.vscene";
    project.uiDocument = project.addAsset("ui/main.rml");
    project.uiFont     = project.addAsset("ui/font.ttf");
    project.addAsset("textures/ui.png");
    project.addAsset("ui/styles/main.rcss");
    project.addAsset("ui/styles/colors.rcss");
    project.addAsset("ui/broken.rml");
    SceneTree(std::make_unique<Node>("UI test")).save(root / project.mainScene);
    project.save(root / "project.vproject");
    const auto pack = run / "ui.vpk";
    VpkArchive::packProject(root / "project.vproject", pack);
    Device      device;
    AssetSource authored(root);
    const auto  baseline = renderUi(device, &authored, "ui/main.rml", "ui/font.ttf");
    require(renderUi(device, nullptr, root / "ui/main.rml", root / "ui/font.ttf").rgba == baseline.rgba,
            "Direct filesystem VGUI changed pixels");
    savePng(baseline, run / "filesystem.png");
    // The original PNG and RCSS remain available: a package miss must never fall back to them.
    fs::remove(root / "ui/main.rml");
    fs::remove(root / "ui/font.ttf");
    AssetSource packaged {VpkArchive(pack)};
    const auto  packed = renderUi(device, &packaged, "ui/main.rml", "ui/font.ttf");
    require(packed.rgba == baseline.rgba && !fs::exists(packaged.resolve("ui/main.rml")),
            "VPK UI changed pixels or extracted the document");
    savePng(packed, run / "vpk.png");
    const auto damagedPack = run / "damaged.vpk";
    fs::copy_file(pack, damagedPack);
    const auto archiveBytes = readSourceFile(pack);
    const auto pngBytes     = readSourceFile(root / "textures/ui.png");
    const auto pngStart     = std::search(archiveBytes.begin(), archiveBytes.end(), pngBytes.begin(), pngBytes.end());
    require(pngStart != archiveBytes.end(), "Could not locate packed PNG for corruption test");
    {
        std::fstream file(damagedPack, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(std::streamoff(pngStart - archiveBytes.begin()));
        file.put('\0');
        require(bool(file), "Could not corrupt packed PNG");
    }
    AssetSource damaged {VpkArchive(damagedPack)};
    requireFailure(
        [&]
        {
            renderUi(device, &damaged, "ui/main.rml", "ui/font.ttf");
        },
        "VGUI silently skipped a corrupt VPK texture");
    fs::copy_file(pack, damagedPack, fs::copy_options::overwrite_existing);
    require(renderUi(device, &damaged, "ui/main.rml", "ui/font.ttf").rgba == baseline.rgba,
            "VGUI did not recover after restoring a corrupt package");
    const auto executable = run / "embedded-ui";
    // A payload prefix suffices to exercise the archive footer; no process is launched.
    std::ofstream(run / "template", std::ios::binary) << "Vultra UI test executable prefix";
    VpkArchive::embedProject(run / "template", pack, executable);
    auto archive = VpkArchive::embeddedProject(executable);
    require(archive.has_value(), "Embedded UI package is missing");
    AssetSource embedded(std::move(*archive));
    const auto  embeddedImage = renderUi(device, &embedded, "ui/main.rml", "ui/font.ttf");
    require(embeddedImage.rgba == baseline.rgba, "Embedded VPK UI changed pixels");
    require(Rml::GetFileInterface() == nullptr, "Destroyed VGUI retained its file interface");
    const auto researchPack = run / "research.vpk";
    VpkArchive::packProject("resources/research.vproject", researchPack);
    {
        const Extent     size {1024, 768};
        ExperimentConfig config {.input = researchPack, .size = size};
        config.importOptions.cacheDirectory = run / "cache";
        ExperimentSession session(device, config);
        Frame             frame(device);
        VGui              gui(device, size, VriFormat_RGBA8_UNORM, session.assetSource());
        const auto&       project = *session.project();
        gui.loadFont(project.asset(*project.uiFont).path);
        gui.loadDocument(project.asset(*project.uiDocument).path);
        session.render();
        const auto scene = session.capture("final");
        Input      input;
        gui.update(input, size);
        auto* cmd = frame.begin();
        gui.draw(cmd, session.output("final"));
        frame.submitAndWait();
        const auto composite = session.capture("final");
        size_t     changed   = 0;
        for (uint32_t y = 0; y < size.height; ++y)
        {
            for (uint32_t x = 0; x < size.width; ++x)
            {
                const auto index   = (y * size.width + x) * 4;
                const bool differs = !std::equal(scene.rgba.begin() + index,
                                                 scene.rgba.begin() + index + 4,
                                                 composite.rgba.begin() + index);
                require(x < size.width / 2 || !differs, "HUD modified pixels outside its panel");
                changed += differs;
            }
        }
        require(changed > 5000 && gui.isChecked("toggle-skybox"), "Packaged Research HUD was not composited");
        savePng(composite, run / "research-hud.png");
    }
    std::cout
        << "Offscreen VGUI passed: filesystem/VPK/embedded pixel parity, styles, fonts, PNGs, input and recovery; "
        << run << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
