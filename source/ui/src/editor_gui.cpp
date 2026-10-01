#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/process.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace vultra
{
    void EditorGui::setPropertyDrawer(std::string id, EditorGuiPropertyDrawer drawer)
    {
        if (id.empty() || !drawer.draw)
        {
            throw std::invalid_argument("Property drawer requires an ID and callback");
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
            return;
        }
        m_PropertyDrawers.push_back({std::move(id), drawer});
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
        std::string layoutFilename(const EditorGuiConfig& config)
        {
            if (!config.persistLayout)
            {
                return {};
            }
            auto file = config.iniFile;
            if (file.empty())
            {
                const auto executableName = executablePath().stem().u8string();
                const auto appName =
                    config.appName.empty() ? std::string(executableName.begin(), executableName.end()) : config.appName;
                if (appName.empty() || appName.find_first_of("<>:\"/\\|?*") != std::string::npos ||
                    appName.back() == '.' || appName.back() == ' ' ||
                    std::any_of(appName.begin(),
                                appName.end(),
                                [](unsigned char c)
                                {
                                    return c < 32;
                                }))
                {
                    throw std::invalid_argument("EditorGuiConfig.appName must be a valid single directory name");
                }
                const std::u8string directory(appName.begin(), appName.end());
                file = std::filesystem::path(".vultra") / directory / "imgui.ini";
            }
            // Pin the path for the lifetime of the context, even if the application changes cwd.
            file = std::filesystem::absolute(file).lexically_normal();
            std::filesystem::create_directories(file.parent_path());
            const auto utf8 = file.u8string();
            return std::string(utf8.begin(), utf8.end());
        }
    } // namespace

    EditorGui::EditorGui(Device& device, Window& window, VriFormat targetFormat, const EditorGuiConfig& config) :
        m_Window(window),
        m_Device(device),
        m_IniFile(layoutFilename(config))
    {
        if (config.multiViewport && targetFormat != VriFormat_BGRA8_UNORM)
        {
            throw std::invalid_argument("ImGui platform windows require the desktop BGRA8_UNORM target format");
        }
        IMGUI_CHECKVERSION();
        m_Context          = ImGui::CreateContext();
        bool platformReady = false;
        try
        {
            auto& io       = ImGui::GetIO();
            io.IniFilename = m_IniFile.empty() ? nullptr : m_IniFile.c_str();
            if (config.docking)
            {
                io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            }
            io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
            // Intentionally use ImGui's supported legacy atlas path, not dynamic textures.
            unsigned char* pixels = nullptr;
            int            width  = 0;
            int            height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            check(vriGetInterface(device.handle, VRI_INTERFACE_IMGUI, sizeof(m_Api), &m_Api), "Get ImGui interface");
            VriImguiDesc desc {};
            desc.uploadQueue = device.queue;
            desc.colorFormat = targetFormat;
            desc.fontAtlas   = pixels;
            desc.fontWidth   = uint32_t(width);
            desc.fontHeight  = uint32_t(height);
            check(m_Api.CreateImgui(device.handle, &desc, &m_Renderer), "Create ImGui renderer");
            io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(m_Api.GetImguiFontView(m_Renderer)));
            platformReady = initializePlatform();
            if (!platformReady)
            {
                throw std::runtime_error("Initialize ImGui window backend");
            }
            setTheme(config.theme);
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
        }
        catch (...)
        {
            if (platformReady)
            {
                shutdownPlatform();
            }
            if (m_Renderer)
            {
                m_Api.DestroyImgui(m_Renderer);
            }
            ImGui::DestroyContext(m_Context);
            throw;
        }
    }

    EditorGui::~EditorGui()
    {
        m_Device.waitIdle();
        ImGui::DestroyPlatformWindows();
        ImGui::GetPlatformIO().ClearRendererHandlers();
        ImGui::GetIO().BackendRendererUserData = nullptr;
        shutdownPlatform();
        m_Api.DestroyImgui(m_Renderer);
        ImGui::DestroyContext(m_Context);
    }

    void EditorGui::setTheme(EditorGuiTheme theme)
    {
        applyEditorGuiTheme(theme);
        m_Theme = theme;
    }

    InputCapture EditorGui::inputCapture() const
    {
        const auto& io = ImGui::GetIO();
        return {io.WantCaptureMouse, io.WantCaptureKeyboard};
    }

    void EditorGui::begin()
    {
        beginPlatformFrame();
        ImGui::NewFrame();
        m_FrameActive = true;
        ++m_FrameSerial;
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            m_Dockspace = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        }
    }

    ImTextureID EditorGui::textureId(Texture& texture)
    {
        return reinterpret_cast<ImTextureID>(texture.view());
    }

    void EditorGui::forgetTexture(Texture& texture)
    {
        m_Api.FreeImguiTexture(m_Renderer, texture.view());
    }

    void EditorGui::DrawData::update(const ImDrawData* draw, Extent framebuffer)
    {
        vertices.clear();
        indices.clear();
        commands.clear();
        data = {};
        if (!draw || draw->TotalVtxCount == 0 || draw->DisplaySize.x <= 0 || draw->DisplaySize.y <= 0 ||
            framebuffer.empty())
        {
            return;
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
                        throw std::runtime_error("Custom ImDrawCallback is outside Vultra's minimal GUI backend");
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
    }

    void EditorGui::upload(Extent framebuffer)
    {
        m_FrameActive = false;
        ImGui::Render();
        m_DrawData.update(ImGui::GetDrawData(), framebuffer);
        if (m_DrawData.data.vertexCount)
        {
            m_Api.UploadImguiData(m_Renderer, &m_DrawData.data);
        }
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
} // namespace vultra
