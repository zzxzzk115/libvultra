#pragma once
#include <vultra/core/base/error.hpp>
#include <vultra/gpu/resources.hpp>
#include <vultra/platform/input/input.hpp>
#include <vultra/ui/editor_gui_frame.hpp>
#include <vultra/ui/editor_gui_inspector.hpp>
#include <vultra/ui/editor_gui_theme.hpp>

#include <imgui.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vultra
{
    class Window;

    struct EditorGuiConfig
    {
        bool                  docking       = true;
        bool                  multiViewport = true;
        bool                  persistLayout = true; // false disables both automatic loading and saving.
        std::string           appName;              // Empty uses the executable stem, independent of the window title.
        std::filesystem::path iniFile;              // Empty uses .vultra/<appName>/imgui.ini in the run directory.
        EditorGuiTheme        theme = EditorGuiTheme::eUnreal;
    };

    // VRI renders native viewports or an explicitly offscreen GUI target. The instance registers its address with
    // ImGui and the window, so it lives behind a unique_ptr and never moves.
    class EditorGui
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<EditorGui>, Error>
        create(Device& device, Window& window, VriFormat targetFormat, const EditorGuiConfig& config = {});
        // Offscreen rendering uses the same VRI renderer, with no window backend or detached viewports.
        [[nodiscard]] static std::expected<std::unique_ptr<EditorGui>, Error>
        create(Device& device, VriFormat targetFormat, const EditorGuiConfig& config);
        // A null window selects offscreen rendering when the target is chosen at runtime.
        [[nodiscard]] static std::expected<std::unique_ptr<EditorGui>, Error>
        create(Device& device, Window* window, VriFormat targetFormat, const EditorGuiConfig& config);
        ~EditorGui();
        EditorGui(const EditorGui&)            = delete;
        EditorGui& operator=(const EditorGui&) = delete;

        [[nodiscard]] std::expected<void, Error> begin();
        [[nodiscard]] std::expected<void, Error> begin(Extent size, float deltaSeconds);

        // Widgets of a frame that is not the current one are a programming error and abort.
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

        bool isCurrentFrame(uint64_t serial) const;

        [[nodiscard]] std::expected<void, Error> setPropertyDrawer(std::string id, EditorGuiPropertyDrawer drawer);
        void                                     removePropertyDrawer(std::string_view id);
        std::optional<EditorGuiPropertyDrawer>   propertyDrawer(std::string_view id) const;
        void                                     setTheme(EditorGuiTheme theme);
        InputCapture                             inputCapture() const;

        EditorGuiTheme theme() const
        {
            return m_Theme;
        }

        ImGuiID dockspaceId() const
        {
            return m_Dockspace;
        }

        // Ends the ImGui frame; call before recording GUI commands.
        [[nodiscard]] std::expected<void, Error>        upload(Extent framebuffer);
        void                                            copy(VriCommandBuffer* cmd); // outside a rendering pass
        void                                            draw(VriCommandBuffer* cmd); // inside a rendering pass
        [[nodiscard]] std::expected<ImTextureID, Error> textureId(Texture& texture);
        // Previous GPU frame must be complete. Call before destroying/resizing a displayed texture.
        [[nodiscard]] std::expected<void, Error> forgetTexture(Texture& texture);
        // Call once after the main GPU frame/present, while sampled textures are still alive.
        [[nodiscard]] std::expected<void, Error> renderPlatformWindows();

    private:
        friend class EditorGuiFrame;
        friend class EditorGuiInspector;
        EditorGui(Device& device, Window* window, std::string iniFile);
        std::expected<void, Error> initialize(VriFormat targetFormat, const EditorGuiConfig& config);
        std::expected<void, Error> checkFrame(uint64_t serial) const;
        void                       requireFrame(uint64_t serial) const;
        void                       beginFrame();
        std::expected<void, Error> updateTextures();
        void                       destroyTextures();
        float                      platformDpiScale() const;
        struct Viewport;

        struct DrawData
        {
            std::expected<void, Error>       update(const ImDrawData* draw, Extent framebuffer);
            VriImguiDrawData                 data {};
            std::vector<VriImguiVertex>      vertices;
            std::vector<ImDrawIdx>           indices;
            std::vector<VriImguiDrawCommand> commands;
        };

        bool         initializePlatform();
        void         shutdownPlatform();
        void         beginPlatformFrame();
        static void* viewportHandle(ImGuiViewport* viewport);
        void         installViewportCallbacks();

        Window*           m_Window;
        Device&           m_Device;
        std::string       m_IniFile;
        VriImguiInterface m_Api {};
        VriImgui*         m_Renderer = nullptr;
        ImGuiContext*     m_Context  = nullptr;
        DrawData          m_DrawData;

        std::unordered_map<VriDescriptor*, Texture> m_Textures;
        bool                                        m_PlatformReady = false;
        // ImGui's renderer callbacks cannot return errors; the first one is reported by renderPlatformWindows().
        std::optional<Error> m_ViewportError;

        float          m_DpiScale    = 1;
        ImGuiID        m_Dockspace   = 0;
        EditorGuiTheme m_Theme       = EditorGuiTheme::eUnreal;
        bool           m_FrameActive = false;
        uint64_t       m_FrameSerial = 0;

        struct PropertyDrawerEntry
        {
            std::string             id;
            EditorGuiPropertyDrawer drawer;
        };

        std::vector<PropertyDrawerEntry> m_PropertyDrawers;
    };
} // namespace vultra
