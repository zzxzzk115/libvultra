#pragma once
#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/ui/editor_gui_frame.hpp>
#include <vultra/ui/editor_gui_inspector.hpp>
#include <vultra/ui/editor_gui_theme.hpp>

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    struct EditorGuiConfig
    {
        bool                  docking       = true;
        bool                  multiViewport = true;
        bool                  persistLayout = true; // false disables both automatic loading and saving.
        std::string           appName;              // Empty uses the executable stem, independent of the window title.
        std::filesystem::path iniFile;              // Empty uses .vultra/<appName>/imgui.ini in the run directory.
        EditorGuiTheme        theme = EditorGuiTheme::eUnreal;
    };

    // The selected desktop backend supplies native windows; VRI renders the main and detached viewports.
    class EditorGui
    {
    public:
        EditorGui(Device& device, Window& window, VriFormat targetFormat, const EditorGuiConfig& config = {});
        ~EditorGui();
        EditorGui(const EditorGui&)            = delete;
        EditorGui& operator=(const EditorGui&) = delete;
        void       begin();

        EditorGuiFrame frame()
        {
            return EditorGuiFrame(*this);
        }

        bool frameActive() const
        {
            return m_FrameActive;
        }

        uint64_t frameSerial() const
        {
            return m_FrameSerial;
        }

        void                                   setPropertyDrawer(std::string id, EditorGuiPropertyDrawer drawer);
        void                                   removePropertyDrawer(std::string_view id);
        std::optional<EditorGuiPropertyDrawer> propertyDrawer(std::string_view id) const;
        void                                   setTheme(EditorGuiTheme theme);
        InputCapture                           inputCapture() const;

        EditorGuiTheme theme() const
        {
            return m_Theme;
        }

        ImGuiID dockspaceId() const
        {
            return m_Dockspace;
        }

        void        upload(Extent framebuffer);  // ends ImGui frame; call before recording GUI commands
        void        copy(VriCommandBuffer* cmd); // outside a rendering pass
        void        draw(VriCommandBuffer* cmd); // inside a rendering pass
        ImTextureID textureId(Texture& texture);
        // Previous GPU frame must be complete. Call before destroying/resizing a displayed texture.
        void forgetTexture(Texture& texture);
        // Call once after the main GPU frame/present, while sampled textures are still alive.
        void renderPlatformWindows();

    private:
        struct Viewport;

        struct DrawData
        {
            void                             update(const ImDrawData* draw, Extent framebuffer);
            VriImguiDrawData                 data {};
            std::vector<VriImguiVertex>      vertices;
            std::vector<ImDrawIdx>           indices;
            std::vector<VriImguiDrawCommand> commands;
        };

        bool              initializePlatform();
        void              shutdownPlatform();
        void              beginPlatformFrame();
        static void*      viewportHandle(ImGuiViewport* viewport);
        void              installViewportCallbacks();
        Window&           m_Window;
        Device&           m_Device;
        std::string       m_IniFile;
        VriImguiInterface m_Api {};
        VriImgui*         m_Renderer = nullptr;
        ImGuiContext*     m_Context  = nullptr;
        DrawData          m_DrawData;
        ImGuiID           m_Dockspace   = 0;
        EditorGuiTheme    m_Theme       = EditorGuiTheme::eUnreal;
        bool              m_FrameActive = false;
        uint64_t          m_FrameSerial = 0;

        struct PropertyDrawerEntry
        {
            std::string             id;
            EditorGuiPropertyDrawer drawer;
        };

        std::vector<PropertyDrawerEntry> m_PropertyDrawers;
    };
} // namespace vultra
