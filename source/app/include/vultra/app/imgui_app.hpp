#pragma once

#include <vultra/app/desktop_app.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <memory>

namespace vultra
{
    // Owns the GUI frame. SceneData rendering and the position of the GUI pass remain explicit.
    class ImGuiApp : public DesktopApp
    {
    public:
        // The desktop services plus the GUI, created before the application like DesktopApp::Services.
        class Services
        {
        public:
            Device& device()
            {
                return m_Context.device();
            }

            Window& window()
            {
                return m_Context.window();
            }

            Swapchain& swapchain()
            {
                return m_Context.swapchain();
            }

            EditorGui& gui()
            {
                return *m_Gui;
            }

        private:
            friend class ImGuiApp;

            Services(RuntimeContext context, std::unique_ptr<EditorGui> gui) :
                m_Context(std::move(context)),
                m_Gui(std::move(gui))
            {
            }

            // Declared first, destroyed last: the GUI uses the window and device.
            RuntimeContext             m_Context;
            std::unique_ptr<EditorGui> m_Gui;
        };

        [[nodiscard]] static std::expected<Services, Error> createServices(const AppConfig&       config,
                                                                           const EditorGuiConfig& guiConfig = {});
        ImGuiApp(Services services, const AppConfig& config);

    protected:
        // Call the base first to finalize UI capture; raw input comes from getWindow().input().
        std::expected<void, Error>         onPreRender() override;
        virtual std::expected<void, Error> onImGui();

        // Overlay an already rendered target, outside any active rendering pass.
        [[nodiscard]] std::expected<void, Error> drawGui(VriCommandBuffer* cmd, Texture& target);

        // For an explicit RenderGraph pass: copy() outside, draw() inside its rendering pass.
        EditorGui& getEditorGui()
        {
            return *m_Gui;
        }

    private:
        std::expected<void, Error> onPostPresent() final;
        std::expected<void, Error> onRenderSkipped() final;
        // Destroyed before DesktopApp's window and device.
        std::unique_ptr<EditorGui> m_Gui;
    };
} // namespace vultra
