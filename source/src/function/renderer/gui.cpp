#include <vultra/core/os/process.hpp>
#include <vultra/function/renderer/gui.hpp>

#include <backends/imgui_impl_glfw.h>

#include <algorithm>

namespace vultra
{
    namespace
    {
        std::string layoutFilename(const GuiConfig& config)
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
                    throw std::invalid_argument("GuiConfig.appName must be a valid single directory name");
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

    Gui::Gui(Device& device, Window& window, VriFormat targetFormat, const GuiConfig& config) :
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
            if (config.multiViewport)
            {
                io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
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
            platformReady = ImGui_ImplGlfw_InitForVulkan(window.handle(), true);
            if (!platformReady)
            {
                throw std::runtime_error("Initialize ImGui GLFW backend");
            }
            setTheme(config.theme);
            if (config.multiViewport)
            {
                installViewportCallbacks();
            }
        }
        catch (...)
        {
            if (platformReady)
            {
                ImGui_ImplGlfw_Shutdown();
            }
            if (m_Renderer)
            {
                m_Api.DestroyImgui(m_Renderer);
            }
            ImGui::DestroyContext(m_Context);
            throw;
        }
    }

    Gui::~Gui()
    {
        m_Device.waitIdle();
        ImGui::DestroyPlatformWindows();
        ImGui::GetPlatformIO().ClearRendererHandlers();
        ImGui::GetIO().BackendRendererUserData = nullptr;
        ImGui_ImplGlfw_Shutdown();
        m_Api.DestroyImgui(m_Renderer);
        ImGui::DestroyContext(m_Context);
    }

    void Gui::setTheme(GuiTheme theme)
    {
        applyGuiTheme(theme);
        m_Theme = theme;
    }

    InputCapture Gui::inputCapture() const
    {
        const auto& io = ImGui::GetIO();
        return {io.WantCaptureMouse, io.WantCaptureKeyboard};
    }

    void Gui::begin()
    {
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            m_Dockspace = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        }
    }

    ImTextureID Gui::textureId(Texture& texture)
    {
        return reinterpret_cast<ImTextureID>(texture.view());
    }

    void Gui::forgetTexture(Texture& texture)
    {
        m_Api.FreeImguiTexture(m_Renderer, texture.view());
    }

    void Gui::DrawData::update(const ImDrawData* draw, Extent framebuffer)
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

    void Gui::upload(Extent framebuffer)
    {
        ImGui::Render();
        m_DrawData.update(ImGui::GetDrawData(), framebuffer);
        if (m_DrawData.data.vertexCount)
        {
            m_Api.UploadImguiData(m_Renderer, &m_DrawData.data);
        }
    }

    void Gui::copy(VriCommandBuffer* cmd)
    {
        if (m_DrawData.data.vertexCount)
        {
            m_Api.CmdCopyImguiData(cmd, m_Renderer);
        }
    }

    void Gui::draw(VriCommandBuffer* cmd)
    {
        if (m_DrawData.data.vertexCount)
        {
            m_Api.CmdDrawImgui(cmd, m_Renderer, &m_DrawData.data);
        }
    }
} // namespace vultra
