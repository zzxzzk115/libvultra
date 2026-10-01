#include "../common/colored_mesh.hpp"
#include "../common/sample.hpp"

#include <vultra/main/app/imgui_app.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
    constexpr float kOrbitRadius = 0.68f;
    constexpr float kTau         = 6.2831853f;
    constexpr int   kSegments    = 64;

    const auto kGuideVertices = []
    {
        std::array<sample::ColoredVertex, 2 * kSegments + 2> vertices {};
        for (int segment = 0; segment < kSegments; ++segment)
        {
            const float start     = kTau * float(segment) / float(kSegments);
            const float end       = kTau * float(segment + 1) / float(kSegments);
            vertices[2 * segment] = {{kOrbitRadius * std::cos(start), kOrbitRadius * std::sin(start), 0}, {1, 1, 1}};
            vertices[2 * segment + 1] = {{kOrbitRadius * std::cos(end), kOrbitRadius * std::sin(end), 0}, {1, 1, 1}};
        }
        vertices[2 * kSegments]     = {{-0.6f, -0.92f, 0}, {1, 1, 1}};
        vertices[2 * kSegments + 1] = {{0.6f, -0.92f, 0}, {1, 1, 1}};
        return vertices;
    }();

    const auto kGuideIndices = []
    {
        std::array<uint32_t, kGuideVertices.size()> indices {};
        for (uint32_t index = 0; index < indices.size(); ++index)
        {
            indices[index] = index;
        }
        return indices;
    }();

    constexpr std::array<sample::ColoredVertex, 3> kShipVertices {
        {{{0, 0.15f, 0}, {1, 1, 1}}, {{-0.11f, -0.11f, 0}, {1, 1, 1}}, {{0.11f, -0.11f, 0}, {1, 1, 1}}}};
    constexpr std::array<uint32_t, 3>              kShipIndices {0, 1, 2};
    constexpr std::array<sample::ColoredVertex, 4> kDiamondVertices {{{{0, 0.12f, 0}, {1, 1, 1}},
                                                                      {{0.12f, 0, 0}, {1, 1, 1}},
                                                                      {{0, -0.12f, 0}, {1, 1, 1}},
                                                                      {{-0.12f, 0, 0}, {1, 1, 1}}}};
    constexpr std::array<uint32_t, 6>              kDiamondIndices {0, 1, 2, 0, 2, 3};

    void drawMesh(sample::ColoredMesh& mesh,
                  VriCommandBuffer*    cmd,
                  vultra::Texture&     target,
                  const glm::mat4&     transform,
                  const glm::vec3&     tint,
                  const float*         clear = nullptr)
    {
        std::copy_n(glm::value_ptr(transform), 16, mesh.parameters.transform.begin());
        mesh.parameters.tint[0] = tint.r;
        mesh.parameters.tint[1] = tint.g;
        mesh.parameters.tint[2] = tint.b;
        mesh.draw(cmd, target, clear);
    }

    class ScriptingApp final : public vultra::ImGuiApp
    {
    public:
        explicit ScriptingApp(const sample::Options& options, const std::filesystem::path& binaryDirectory) :
            vultra::ImGuiApp({.title = "Vultra | Scripted Arena", .size = {1280, 720}}),
            m_Scene(std::make_unique<vultra::Node>("Scripted Arena")),
            m_Scripts(m_Scene, true),
            m_Options(options),
            m_Guides(getDevice(),
                     getSwapchain().format(),
                     kGuideVertices,
                     kGuideIndices,
                     VriPrimitiveTopology_LineList),
            m_ShipMesh(getDevice(), getSwapchain().format(), kShipVertices, kShipIndices),
            m_Diamond(getDevice(), getSwapchain().format(), kDiamondVertices, kDiamondIndices)
        {
            m_Ship     = &m_Scene.addChild(m_Scene.root(), std::make_unique<vultra::Node>("Ship"));
            m_Beacon   = &m_Scene.addChild(m_Scene.root(), std::make_unique<vultra::Node>("Beacon"));
            m_Throttle = &m_Scene.addChild(m_Scene.root(), std::make_unique<vultra::Node>("Throttle"));
            m_Ship->setLocalTransform(glm::translate(glm::mat4(1), {kOrbitRadius, 0, 0}));
            m_Beacon->setLocalTransform(
                glm::translate(glm::mat4(1), {kOrbitRadius * std::cos(0.6f), kOrbitRadius * std::sin(0.6f), 0}));
            m_Throttle->setLocalTransform(glm::translate(glm::mat4(1), {0, -0.92f, 0}));
#ifdef _WIN32
            m_Scripts.addExtension(binaryDirectory / "example-native-plugin.dll");
#else
            m_Scripts.addExtension(binaryDirectory / "libexample-native-plugin.so");
#endif
            m_Scripts.add({vultra::ScriptModule::Language::eCSharp,
                           {},
                           "VultraScript.ThrottleController",
                           m_Throttle->idInScene().value},
                          "build/.tmp/scripting-managed/VultraScript.dll");
#ifdef _WIN32
            const auto nativeModule = binaryDirectory / "example-native-cpp-plugin.dll";
#else
            const auto nativeModule = binaryDirectory / "libexample-native-cpp-plugin.so";
#endif
            m_Scripts.addExtension(nativeModule);
            m_Scripts.add({vultra::ScriptModule::Language::eNative, {}, "ShipOrbit", m_Ship->idInScene().value},
                          nativeModule);
            m_Scripts.add({vultra::ScriptModule::Language::eLua, {}, {}, m_Beacon->idInScene().value},
                          "examples/scripting/lua/scene_probe.lua");
        }

    private:
        void onUpdate(float deltaSeconds) override
        {
            if (m_CheckNow)
            {
                m_CheckNow = false;
                m_Scripts.reloadChanged(true);
            }
            m_Scripts.update(deltaSeconds);
        }

        void onImGui() override
        {
            auto        ui       = getEditorGui().frame();
            const auto* viewport = ImGui::GetMainViewport();
            ui.setNextWindowPos({viewport->Pos.x + viewport->Size.x - 430, viewport->Pos.y + 45}, ImGuiCond_Always);
            ui.setNextWindowSize({400, 320}, ImGuiCond_Always);
            if (vultra::EditorGuiWindow window(ui, "Scripted arena"); window)
            {
                ui.textUnformatted("Two extensions, three scripts, one scene:");
                ui.textUnformatted("C++ moves the ship. Lua handles pickups.");
                ui.textUnformatted("C# animates the throttle; buttons take over.");
                ui.separator();
                m_Scripts.gui(getEditorGui());
                ui.separator();
                ui.textUnformatted("Edit Lua or rebuild a plugin to hot reload.");
                if (ui.button("Retry failed reloads"))
                {
                    m_CheckNow = true;
                }
                if (!m_Scripts.lastReloadError().empty())
                {
                    ui.textWrapped("%s", std::string(m_Scripts.lastReloadError()).c_str());
                }
            }
        }

        void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
        {
            target.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[4] {0.035f, 0.055f, 0.095f, 1.0f};
            const float aspect     = float(target.desc.width) / float(target.desc.height);
            const auto  projection = glm::scale(glm::mat4(1), {1.0f / aspect, 1.0f, 1.0f});
            const auto  arena      = glm::translate(glm::mat4(1), {-0.36f, 0, 0});
            drawMesh(m_Guides, cmd, target, projection * arena, {0.29f, 0.42f, 0.58f}, clear);

            const auto ship     = glm::vec3(m_Ship->localTransform()[3]);
            const auto beacon   = glm::vec3(m_Beacon->localTransform()[3]);
            const auto throttle = glm::vec3(m_Throttle->localTransform()[3]);
            drawMesh(m_Diamond,
                     cmd,
                     target,
                     projection * arena * glm::translate(glm::mat4(1), beacon),
                     {1.0f, 0.76f, 0.19f});
            const auto shipTransform =
                glm::translate(glm::mat4(1), ship) * glm::rotate(glm::mat4(1), std::atan2(ship.y, ship.x), {0, 0, 1});
            drawMesh(m_ShipMesh, cmd, target, projection * arena * shipTransform, {0.22f, 0.80f, 1.0f});
            drawMesh(m_Diamond,
                     cmd,
                     target,
                     projection * arena * glm::translate(glm::mat4(1), throttle) *
                         glm::scale(glm::mat4(1), {0.6f, 0.6f, 1.0f}),
                     {0.98f, 0.39f, 0.40f});
            drawGui(cmd, target);
        }

        void onPostRender(vultra::Texture& target) override
        {
            sample::captureFrame(m_Options, frameCount(), getDevice(), target);
        }

        vultra::SceneTree   m_Scene;
        vultra::ScriptHost  m_Scripts;
        sample::Options     m_Options;
        sample::ColoredMesh m_Guides;
        sample::ColoredMesh m_ShipMesh;
        sample::ColoredMesh m_Diamond;
        vultra::Node*       m_Ship     = nullptr;
        vultra::Node*       m_Beacon   = nullptr;
        vultra::Node*       m_Throttle = nullptr;
        bool                m_CheckNow = false;
    };
} // namespace

int main(int argc, char** argv)
try
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return 0;
    }
    ScriptingApp app(*options, std::filesystem::absolute(argv[0]).parent_path());
    app.run(options->frames);
    vultra::Logger::app().info("Scripting example: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
