#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/process.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <algorithm>
#include <cmath>
#include <system_error>
#include <utility>

namespace vultra
{
    std::expected<void, Error> EditorGui::setPropertyDrawer(std::string id, EditorGuiPropertyDrawer drawer)
    {
        if (id.empty() || !drawer.draw)
        {
            return makeError("Property drawer requires an ID and callback");
        }
        const auto existing = std::find_if(m_PropertyDrawers.begin(),
                                           m_PropertyDrawers.end(),
                                           [&](const PropertyDrawerEntry& entry)
                                           {
                                               return entry.id == id;
                                           });
        if (existing != m_PropertyDrawers.end())
        {
            existing->drawer = drawer;
            return {};
        }
        m_PropertyDrawers.push_back({std::move(id), drawer});
        return {};
    }

    void EditorGui::removePropertyDrawer(std::string_view id)
    {
        std::erase_if(m_PropertyDrawers,
                      [&](const PropertyDrawerEntry& entry)
                      {
                          return entry.id == id;
                      });
    }

    std::optional<EditorGuiPropertyDrawer> EditorGui::propertyDrawer(std::string_view id) const
    {
        const auto existing = std::find_if(m_PropertyDrawers.begin(),
                                           m_PropertyDrawers.end(),
                                           [&](const PropertyDrawerEntry& entry)
                                           {
                                               return entry.id == id;
                                           });
        if (existing == m_PropertyDrawers.end())
        {
            return std::nullopt;
        }
        return existing->drawer;
    }

    namespace
    {
        class ContextScope
        {
        public:
            explicit ContextScope(ImGuiContext* context) :
                m_Previous(ImGui::GetCurrentContext())
            {
                ImGui::SetCurrentContext(context);
            }

            ~ContextScope()
            {
                ImGui::SetCurrentContext(m_Previous);
            }

            ContextScope(const ContextScope&)            = delete;
            ContextScope& operator=(const ContextScope&) = delete;

        private:
            ImGuiContext* m_Previous;
        };

        // Reuse the bundled scalable UI font (SIL OFL: resources/ui/LICENSE.txt).
        constexpr unsigned char kEditorFont[] = {
#include "LatoLatin-Regular.ttf.h"
        };

        std::expected<std::string, Error> layoutFilename(const EditorGuiConfig& config)
        {
            if (!config.persistLayout)
            {
                return std::string {};
            }
            auto file = config.iniFile;
            if (file.empty())
            {
                std::string appName = config.appName;
                if (appName.empty())
                {
                    const auto executable = executablePath();
                    if (!executable)
                    {
                        return std::unexpected(executable.error());
                    }
                    const auto executableName = executable->stem().u8string();
                    appName.assign(executableName.begin(), executableName.end());
                }
                if (appName.empty() || appName.find_first_of("<>:\"/\\|?*") != std::string::npos ||
                    appName.back() == '.' || appName.back() == ' ' ||
                    std::any_of(appName.begin(),
                                appName.end(),
                                [](unsigned char c)
                                {
                                    return c < 32;
                                }))
                {
                    return makeError("EditorGuiConfig.appName must be a valid single directory name");
                }
                const std::u8string directory(appName.begin(), appName.end());
                file = std::filesystem::path(".vultra") / directory / "imgui.ini";
            }
            // Pin the path for the lifetime of the context, even if the application changes cwd.
            std::error_code error;
            const auto      absolute = std::filesystem::absolute(file, error);
            if (error)
            {
                return makeError("Resolve GUI layout path {}: {}", file.string(), error.message());
            }
            file = absolute.lexically_normal();
            std::filesystem::create_directories(file.parent_path(), error);
            if (error)
            {
                return makeError("Create GUI layout directory {}: {}", file.parent_path().string(), error.message());
            }
            const auto utf8 = file.u8string();
            return std::string(utf8.begin(), utf8.end());
        }
    } // namespace

    EditorGui::EditorGui(Device& device, Window* window, std::string iniFile) :
        m_Window(window),
        m_Device(device),
        m_IniFile(std::move(iniFile))
    {
    }

    std::expected<std::unique_ptr<EditorGui>, Error>
    EditorGui::create(Device& device, Window& window, VriFormat targetFormat, const EditorGuiConfig& config)
    {
        return create(device, &window, targetFormat, config);
    }

    std::expected<std::unique_ptr<EditorGui>, Error>
    EditorGui::create(Device& device, VriFormat targetFormat, const EditorGuiConfig& config)
    {
        return create(device, nullptr, targetFormat, config);
    }

    std::expected<std::unique_ptr<EditorGui>, Error>
    EditorGui::create(Device& device, Window* window, VriFormat targetFormat, const EditorGuiConfig& config)
    {
        auto iniFile = layoutFilename(config);
        if (!iniFile)
        {
            return std::unexpected(iniFile.error());
        }
#ifndef VULTRA_EDITOR_GUI_WINDOW
        if (window)
        {
            return makeError("Window-backed EditorGui requires the window feature");
        }
#endif
        if (!window && config.multiViewport)
        {
            return makeError("Offscreen ImGui cannot create detached viewports");
        }
        if (config.multiViewport && targetFormat != VriFormat_BGRA8_UNORM)
        {
            return makeError("ImGui platform windows require the desktop BGRA8_UNORM target format");
        }
        auto*                      previous = ImGui::GetCurrentContext();
        std::unique_ptr<EditorGui> gui(new EditorGui(device, window, std::move(*iniFile)));
        if (auto result = gui->initialize(targetFormat, config); !result)
        {
            // The destructor releases what initialize() created; restore the caller's context afterwards.
            gui.reset();
            ImGui::SetCurrentContext(previous);
            return std::unexpected(result.error());
        }
        return gui;
    }

    std::expected<void, Error> EditorGui::initialize(VriFormat targetFormat, const EditorGuiConfig& config)
    {
        IMGUI_CHECKVERSION();
        m_Context = ImGui::CreateContext();
        // CreateContext preserves an existing current context; initialize this instance's IO and renderer.
        ImGui::SetCurrentContext(m_Context);
        auto& io       = ImGui::GetIO();
        io.IniFilename = m_IniFile.empty() ? nullptr : m_IniFile.c_str();
        if (config.docking)
        {
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        }
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
        io.ConfigDpiScaleFonts     = true;
        io.ConfigDpiScaleViewports = true;
        ImFontConfig fontConfig;
        fontConfig.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kEditorFont), sizeof(kEditorFont), 16, &fontConfig);
        if (auto result = check(vriGetInterface(m_Device.handle, VRI_INTERFACE_IMGUI, sizeof(m_Api), &m_Api),
                                "Get ImGui interface");
            !result)
        {
            return result;
        }
        VriImguiDesc desc {};
        desc.uploadQueue = m_Device.queue;
        desc.colorFormat = targetFormat;
        if (auto result = check(m_Api.CreateImgui(m_Device.handle, &desc, &m_Renderer), "Create ImGui renderer");
            !result)
        {
            return result;
        }
#ifdef VULTRA_EDITOR_GUI_WINDOW
        if (m_Window)
        {
            m_PlatformReady = initializePlatform();
            if (!m_PlatformReady)
            {
                return makeError("Initialize ImGui window backend");
            }
            m_DpiScale = platformDpiScale();
            ImGui::GetStyle().ScaleAllSizes(m_DpiScale);
            ImGui::GetStyle().FontScaleDpi = m_DpiScale;
            Logger::core().info("Editor UI DPI scale: {}", m_DpiScale);
        }
#endif
        setTheme(config.theme);
#ifdef VULTRA_EDITOR_GUI_WINDOW
        if (config.multiViewport)
        {
            if (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports)
            {
                io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
                installViewportCallbacks();
            }
            else
            {
                Logger::core().warn(
                    "The window system does not support detached ImGui viewports; docking remains available");
            }
        }
#endif
        return {};
    }

    // Also releases a partially initialized instance after initialize() failed.
    EditorGui::~EditorGui()
    {
        if (!m_Context)
        {
            return;
        }
        auto* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(m_Context);
        m_Device.waitIdleForDestruction();
        ImGui::DestroyPlatformWindows();
        ImGui::GetPlatformIO().ClearRendererHandlers();
        ImGui::GetIO().BackendRendererUserData = nullptr;
#ifdef VULTRA_EDITOR_GUI_WINDOW
        if (m_PlatformReady)
        {
            shutdownPlatform();
        }
#endif
        destroyTextures();
        if (m_Renderer)
        {
            m_Api.DestroyImgui(m_Renderer);
        }
        ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;
        ImGui::DestroyContext(m_Context);
        if (previous != m_Context)
        {
            ImGui::SetCurrentContext(previous);
        }
    }

    void EditorGui::setTheme(EditorGuiTheme theme)
    {
        const ContextScope context(m_Context);
        applyEditorGuiTheme(theme);
        m_Theme = theme;
    }

    InputCapture EditorGui::inputCapture() const
    {
        const ContextScope context(m_Context);
        const auto&        io = ImGui::GetIO();
        return {io.WantCaptureMouse, io.WantCaptureKeyboard};
    }

    std::expected<void, Error> EditorGui::begin()
    {
        if (m_FrameActive)
        {
            return makeError("Complete the current EditorGui frame before beginning another");
        }
        ImGui::SetCurrentContext(m_Context);
        if (!m_Window)
        {
            return makeError("Offscreen ImGui requires an explicit frame extent and time step");
        }
#ifdef VULTRA_EDITOR_GUI_WINDOW
        beginPlatformFrame();
        const auto scale = platformDpiScale();
        if (std::abs(scale - m_DpiScale) > 0.001f)
        {
            ImGui::GetStyle().ScaleAllSizes(scale / m_DpiScale);
            m_DpiScale = scale;
        }
#endif
        beginFrame();
        return {};
    }

    std::expected<void, Error> EditorGui::begin(Extent size, float deltaSeconds)
    {
        if (m_FrameActive)
        {
            return makeError("Complete the current EditorGui frame before beginning another");
        }
        ImGui::SetCurrentContext(m_Context);
        if (m_Window || size.empty() || !std::isfinite(deltaSeconds) || deltaSeconds <= 0)
        {
            return makeError("Offscreen ImGui requires no window, positive extent and finite time step");
        }
        auto& io                   = ImGui::GetIO();
        io.DisplaySize             = {float(size.width), float(size.height)};
        io.DisplayFramebufferScale = {1, 1};
        io.DeltaTime               = deltaSeconds;
        beginFrame();
        return {};
    }

    void EditorGui::beginFrame()
    {
        ImGui::NewFrame();
        m_FrameActive = true;
        ++m_FrameSerial;
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            m_Dockspace = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        }
    }

    bool EditorGui::isCurrentFrame(uint64_t serial) const
    {
        return m_FrameActive && serial == m_FrameSerial && ImGui::GetCurrentContext() == m_Context;
    }

    std::expected<void, Error> EditorGui::checkFrame(uint64_t serial) const
    {
        if (!isCurrentFrame(serial))
        {
            return makeError("EditorGui frame expired or belongs to a different current context");
        }
        return {};
    }

    void EditorGui::requireFrame(uint64_t serial) const
    {
        if (auto result = checkFrame(serial); !result)
        {
            panic(result.error().message);
        }
    }

    std::expected<ImTextureID, Error> EditorGui::textureId(Texture& texture)
    {
        const auto view = texture.view();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        return reinterpret_cast<ImTextureID>(*view);
    }

    std::expected<void, Error> EditorGui::forgetTexture(Texture& texture)
    {
        const auto view = texture.view();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        m_Api.FreeImguiTexture(m_Renderer, *view);
        return {};
    }

    std::expected<void, Error> EditorGui::DrawData::update(const ImDrawData* draw, Extent framebuffer)
    {
        vertices.clear();
        indices.clear();
        commands.clear();
        data = {};
        if (!draw || draw->TotalVtxCount == 0 || draw->DisplaySize.x <= 0 || draw->DisplaySize.y <= 0 ||
            framebuffer.empty())
        {
            return {};
        }
        // Normalize positions and clipping to framebuffer pixels. VRI's scissor API uses pixels;
        // ImGui uses desktop coordinates, which may have a different scale on each monitor.
        const ImVec2 scale {float(framebuffer.width) / draw->DisplaySize.x,
                            float(framebuffer.height) / draw->DisplaySize.y};
        const auto   position = [&](float x, float y)
        {
            return ImVec2((x - draw->DisplayPos.x) * scale.x, (y - draw->DisplayPos.y) * scale.y);
        };
        for (const auto* list : draw->CmdLists)
        {
            const auto vertexBase = uint32_t(vertices.size());
            const auto indexBase  = uint32_t(indices.size());
            for (const auto& vertex : list->VtxBuffer)
            {
                const auto pixel = position(vertex.pos.x, vertex.pos.y);
                vertices.push_back({{pixel.x, pixel.y}, {vertex.uv.x, vertex.uv.y}, vertex.col});
            }
            indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
            for (const auto& command : list->CmdBuffer)
            {
                if (command.UserCallback)
                {
                    // VRI binds its full render state for each draw. Never call the reset sentinel.
                    if (command.UserCallback != ImDrawCallback_ResetRenderState)
                    {
                        return makeError("Custom ImDrawCallback is outside Vultra's minimal GUI backend");
                    }
                    continue;
                }
                const auto clipMin = position(command.ClipRect.x, command.ClipRect.y);
                const auto clipMax = position(command.ClipRect.z, command.ClipRect.w);
                commands.push_back({{clipMin.x, clipMin.y, clipMax.x, clipMax.y},
                                    command.ElemCount,
                                    indexBase + command.IdxOffset,
                                    int32_t(vertexBase + command.VtxOffset),
                                    reinterpret_cast<VriDescriptor*>(command.GetTexID())});
            }
        }
        data.vertices          = vertices.data();
        data.vertexCount       = uint32_t(vertices.size());
        data.indices           = indices.data();
        data.indexCount        = uint32_t(indices.size());
        data.indexSize         = sizeof(ImDrawIdx);
        data.commands          = commands.data();
        data.commandCount      = uint32_t(commands.size());
        data.displayPos[0]     = 0;
        data.displayPos[1]     = 0;
        data.displaySize[0]    = float(framebuffer.width);
        data.displaySize[1]    = float(framebuffer.height);
        data.framebufferWidth  = framebuffer.width;
        data.framebufferHeight = framebuffer.height;
        return {};
    }

    std::expected<void, Error> EditorGui::upload(Extent framebuffer)
    {
        const ContextScope context(m_Context);
        if (auto result = checkFrame(m_FrameSerial); !result)
        {
            return result;
        }
        m_FrameActive = false;
        ImGui::Render();
        if (auto result = updateTextures(); !result)
        {
            return result;
        }
        if (auto result = m_DrawData.update(ImGui::GetDrawData(), framebuffer); !result)
        {
            return result;
        }
        if (m_DrawData.data.vertexCount)
        {
            m_Api.UploadImguiData(m_Renderer, &m_DrawData.data);
        }
        return {};
    }

    void EditorGui::copy(VriCommandBuffer* cmd)
    {
        if (m_DrawData.data.vertexCount)
        {
            m_Api.CmdCopyImguiData(cmd, m_Renderer);
        }
    }

    void EditorGui::draw(VriCommandBuffer* cmd)
    {
        if (m_DrawData.data.vertexCount)
        {
            m_Api.CmdDrawImgui(cmd, m_Renderer, &m_DrawData.data);
        }
    }

    std::expected<void, Error> EditorGui::renderPlatformWindows()
    {
        const ContextScope context(m_Context);
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        if (m_ViewportError)
        {
            auto error = std::move(*m_ViewportError);
            m_ViewportError.reset();
            return std::unexpected(std::move(error));
        }
        return {};
    }
} // namespace vultra
