#include "vultra/function/imgui/imgui_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/structs/sampler_info.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/imgui/imgui_theme.hpp"
#include "vultra/function/rendering/runtime_profiler.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#if defined(__ANDROID__)
#include "vultra/platform/android/android_native_window.hpp"
#elif !defined(__EMSCRIPTEN__)
#include "vultra/platform/sdl/sdl_window.hpp"
#else
#endif

#include <font_headers/materialdesignicons_webfont.ttf.binfont.h>
#include <font_headers/color_emoji.ttf.binfont.h>  // bundled colour-emoji font (builtin/fonts)
#include <font_headers/wqy_microhei.ttf.binfont.h>  // bundled Simplified-Chinese font (WenQuanYi Micro Hei)

#include <vbase/core/exe_path.hpp>

#include <filesystem>
#include <type_traits>

#include <IconsMaterialDesignIcons.h>
#include <ImGuiAl/fonts/CousineRegular.inl>
#include <ImGuiAl/fonts/RobotoBold.inl>
#include <ImGuiAl/fonts/RobotoRegular.inl>
#include <ImGuizmo/ImGuizmo.h>
#include <imgui.h>
#include <imgui_graphnode/imgui_graphnode.h>
#include <imgui_internal.h>
#include <imnodes/imnodes.h>
#include <implot/implot.h>

#ifdef IMGUI_ENABLE_FREETYPE
#include <imgui_freetype.h> // ImGuiFreeTypeLoaderFlags_LoadColor (colorful emoji)
#endif

#include <lz4.h> // decompress lz4-block-compressed embedded fonts (builtin font_task)

namespace
{
    vultra::rhi::Sampler resolveImGuiSampler(vultra::rhi::RenderDevice&  rd,
                                             const vultra::rhi::Texture& texture,
                                             vultra::rhi::Sampler        sampler)
    {
        if (sampler)
            return sampler;

        sampler = texture.getSampler();
        if (sampler)
            return sampler;

        return rd.getSampler(vultra::rhi::SamplerInfo {
            .magFilter    = vultra::rhi::TexelFilter::eNearest,
            .minFilter    = vultra::rhi::TexelFilter::eNearest,
            .mipmapMode   = vultra::rhi::MipmapMode::eNearest,
            .addressModeS = vultra::rhi::SamplerAddressMode::eClampToEdge,
            .addressModeT = vultra::rhi::SamplerAddressMode::eClampToEdge,
            .addressModeR = vultra::rhi::SamplerAddressMode::eClampToEdge,
        });
    }

    // Merge the bundled colour-emoji font into the current default font so the UI (notably the AI
    // Decompress an lz4-block-compressed TTF (a `<sym>_lz4` / `<sym>_lz4_size` / `<sym>_size` blob
    // emitted by builtin/xmake.lua's font_task) and hand it to the atlas. ImGui takes ownership of
    // the decompressed buffer (allocated with ImGui::MemAlloc) and frees it with the atlas.
    ImFont* addCompressedFontTTF(ImGuiIO&             io,
                                 const unsigned char* compressed,
                                 int                  compressedSize,
                                 int                  rawSize,
                                 float                sizePixels,
                                 const ImFontConfig*  cfgIn,
                                 const ImWchar*       ranges = nullptr)
    {
        void* raw = ImGui::MemAlloc(static_cast<std::size_t>(rawSize));
        if (!raw)
            return nullptr;
        const int n = LZ4_decompress_safe(
            reinterpret_cast<const char*>(compressed), static_cast<char*>(raw), compressedSize, rawSize);
        if (n != rawSize) // corrupt blob / size mismatch: don't hand a bad buffer to the atlas
        {
            ImGui::MemFree(raw);
            return nullptr;
        }
        ImFontConfig cfg         = cfgIn ? *cfgIn : ImFontConfig {};
        cfg.FontDataOwnedByAtlas = true; // ImGui frees `raw` (ImGui::MemAlloc) when the atlas dies
        return io.Fonts->AddFontFromMemoryTTF(raw, rawSize, sizePixels, &cfg, ranges);
    }

    // chat) renders emoji instead of tofu boxes. Same compiled-in font on every platform — FreeType
    // rasterises the COLR/CPAL colour glyphs identically on Windows/Linux/macOS/Android/web, so no
    // per-platform system font is needed. Requires the FreeType loader (IMGUI_ENABLE_FREETYPE) and a
    // 32-bit ImWchar (IMGUI_USE_WCHAR32 — emoji live above U+FFFF); a no-op otherwise.
    //
    // Font: builtin/fonts/color_emoji.ttf = Twemoji Mozilla, a COLRv0 colour font (CC-BY 4.0 /
    // redistributable, ~1.4MB). It MUST be COLRv0 (or a bitmap CBDT/sbix font): imgui_freetype only
    // rasterises COLRv0 layers and colour bitmaps via FT_LOAD_COLOR — it does NOT composite COLRv1
    // paint graphs, so a COLRv1 font (e.g. Noto-COLRv1) renders blank. To change emoji set, keep to
    // a COLRv0/bitmap .ttf, drop it in builtin/fonts, and point this at the regenerated symbol.
    bool tryMergeColorEmoji([[maybe_unused]] ImGuiIO& io, [[maybe_unused]] float sizePixels)
    {
#ifdef IMGUI_ENABLE_FREETYPE
        ImFontConfig cfg {};
        cfg.MergeMode = true; // fold emoji glyphs into the preceding (default) font
        cfg.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_LoadColor;
        // 1.92 dynamic fonts load glyphs on demand, so no explicit emoji glyph range is needed.
        return addCompressedFontTTF(io,
                                    color_emoji_ttf_lz4,
                                    static_cast<int>(color_emoji_ttf_lz4_size),
                                    static_cast<int>(color_emoji_ttf_size),
                                    sizePixels,
                                    &cfg) != nullptr;
#else
        return false;
#endif
    }

    template<typename T>
    T toImGuiTextureId(std::uintptr_t textureId)
    {
        if constexpr (std::is_pointer_v<T>)
        {
            return reinterpret_cast<T>(textureId);
        }
        else
        {
            return static_cast<T>(textureId);
        }
    }

    template<typename T>
    std::uintptr_t fromImGuiTextureId(T textureId)
    {
        if constexpr (std::is_pointer_v<T>)
        {
            return reinterpret_cast<std::uintptr_t>(textureId);
        }
        else
        {
            return static_cast<std::uintptr_t>(textureId);
        }
    }

    std::string get_imgui_config_file_full_path(const std::string& writableRoot, const char* imguiIniFile)
    {
        if (imguiIniFile == nullptr || imguiIniFile[0] == '\0')
            return {};

#if defined(__EMSCRIPTEN__)
        // Browser runtime has no stable executable path (/proc/self/exe is unavailable),
        // so we only use an explicit writable root if provided.
        if (writableRoot.empty())
        {
            return {};
        }
#endif
        const std::filesystem::path root =
            !writableRoot.empty() ? std::filesystem::path(writableRoot) : vbase::executable_dir();
        return (root / imguiIniFile).generic_string();
    }
} // namespace

namespace vultra
{
    bool ImGuiSystem::onInit()
    {
        VULTRA_CORE_INFO("[ImGuiSystem] Initializing...");

        try
        {
            auto& renderBackendService = ctx().services.require<IRenderBackendService>();
            auto& windowService        = ctx().services.require<IWindowService>();

            const auto& config = ctx().config.imgui;
            initImGui(renderBackendService.renderDevice(),
                      renderBackendService.swapchain(),
                      windowService.window(),
                      config.enableMultiview,
                      config.enableDocking,
                      ctx().config.writableRoot,
                      config.imguiIniFile.c_str());
            renderBackendService.imguiBackend().init(windowService.window(),
                                                     renderBackendService.renderDevice(),
                                                     renderBackendService.swapchain(),
                                                     config.enableMultiview,
                                                     config.enableDocking);

            VULTRA_CORE_TRACE("[ImGuiSystem] Providing IImGuiService");
            ctx().services.provide<IImGuiService>(this);
            return true;
        }
        catch (const std::exception& e)
        {
            VULTRA_CORE_ERROR("[ImGuiSystem] Initialization failed: {}", e.what());
            return false;
        }
        catch (...)
        {
            VULTRA_CORE_ERROR("[ImGuiSystem] Initialization failed: unknown exception");
            return false;
        }
    }

    void ImGuiSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[ImGuiSystem] Shutting down");
        collectRetiredTextures(true);
        ctx().services.require<IRenderBackendService>().imguiBackend().shutdown(
            ctx().config.writableRoot, ctx().config.imgui.imguiIniFile.c_str());
        shutdownImGui(ctx().config.writableRoot, ctx().config.imgui.imguiIniFile.c_str());
    }

    void ImGuiSystem::begin()
    {
        RuntimeProfiler::ExternalScope perf {"ImGuiSystem::begin"};
        auto& window               = ctx().services.require<IWindowService>().window();
        auto& renderBackendService = ctx().services.require<IRenderBackendService>();
        renderBackendService.imguiBackend().beginFrame(window);

        // Backend new-frame hooks own DisplaySize on native platforms. Only touch
        // ImGui's display metrics when the cached window metrics actually changed.
        {
            ImGuiIO&   io       = ImGui::GetIO();
            const auto extent   = window.getExtent();
            const auto fbExtent = window.getFrameBufferExtent();

            const bool displaySizeInvalid = io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f;
            const bool metricsChanged =
                !m_DisplayMetricsInitialized || extent != m_LastDisplayExtent || fbExtent != m_LastFramebufferExtent;

            if (displaySizeInvalid || metricsChanged)
            {
                const float width          = static_cast<float>(std::max(extent.x, 1));
                const float height         = static_cast<float>(std::max(extent.y, 1));
                const float fbWidth        = static_cast<float>(std::max(fbExtent.x, 1));
                const float fbHeight       = static_cast<float>(std::max(fbExtent.y, 1));
                io.DisplaySize             = ImVec2(width, height);
                io.DisplayFramebufferScale = ImVec2(fbWidth / width, fbHeight / height);

                m_LastDisplayExtent         = extent;
                m_LastFramebufferExtent     = fbExtent;
                m_DisplayMetricsInitialized = true;
            }
        }

        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }

    void ImGuiSystem::render(rhi::CommandBuffer& cb, const rhi::FramebufferInfo& framebufferInfo)
    {
        RuntimeProfiler::ExternalScope perf {"ImGuiSystem::render"};
        RHI_GPU_ZONE(cb, "ImGuiRenderer::render");

        auto&                renderBackendService = ctx().services.require<IRenderBackendService>();
        rhi::FramebufferInfo fbInfoCopy           = framebufferInfo;

        if (!fbInfoCopy.colorAttachments.empty() &&
            renderBackendService.renderDevice().getBackendApi() == rhi::RenderBackendApi::eWebGPU)
        {
            // Keep WebGPU ImGui overlay deterministic: always render to the currently presented backbuffer.
            // Some renderer paths may pass intermediate targets, which makes ImGui invisible on final present.
            fbInfoCopy.colorAttachments[0].target = &renderBackendService.backbuffer();
        }

        // Clear value is handled by the RenderSystem, we don't want ImGui to clear again.
        if (fbInfoCopy.colorAttachments[0].clearValue.has_value())
        {
            fbInfoCopy.colorAttachments[0].clearValue = std::nullopt;
        }
        else
        {
            fbInfoCopy.colorAttachments[0].clearValue = glm::vec4 {0, 0, 0, 1};
        }

        cb.beginRendering(fbInfoCopy);

        renderBackendService.imguiBackend().render(cb);

        cb.endRendering();
    }

    void ImGuiSystem::end() {}

    void ImGuiSystem::postRender()
    {
        RuntimeProfiler::ExternalScope perf {"ImGuiSystem::postRender"};
        ctx().services.require<IRenderBackendService>().imguiBackend().postRender();
        ++m_PostRenderFrame;
        collectRetiredTextures();
    }

    IImGuiService::TextureID ImGuiSystem::addTexture(const rhi::Texture& texture, rhi::Sampler sampler)
    {
        auto& renderBackendService = ctx().services.require<IRenderBackendService>();
        auto& rd                   = renderBackendService.renderDevice();
        return toImGuiTextureId<IImGuiService::TextureID>(
            renderBackendService.imguiBackend().addTexture(texture, resolveImGuiSampler(rd, texture, sampler)));
    }

    void ImGuiSystem::removeTexture(TextureID& textureID)
    {
        if (!textureID)
            return;

        constexpr uint64_t kDescriptorReleaseDelayFrames = 4u;
        const auto         backendTextureId              = fromImGuiTextureId(textureID);
        m_RetiredTextures.push_back(RetiredTexture {
            .backendTextureId = backendTextureId,
            .releaseFrame     = m_PostRenderFrame + kDescriptorReleaseDelayFrames,
        });
        textureID = 0;
    }

    void ImGuiSystem::collectRetiredTextures(const bool force)
    {
        RuntimeProfiler::ExternalScope perf {"ImGuiSystem::collectRetiredTextures"};
        if (m_RetiredTextures.empty())
            return;

        auto& renderBackendService = ctx().services.require<IRenderBackendService>();
        auto& backend              = renderBackendService.imguiBackend();

        std::size_t out = 0;
        for (auto& retired : m_RetiredTextures)
        {
            if (force || m_PostRenderFrame >= retired.releaseFrame)
            {
                backend.removeTexture(retired.backendTextureId);
            }
            else
            {
                m_RetiredTextures[out++] = retired;
            }
        }
        m_RetiredTextures.resize(out);
    }

    void ImGuiSystem::processEvent(const os::GeneralWindowEvent& event)
    {
        ctx().services.require<IRenderBackendService>().imguiBackend().processEvent(event);
    }

    std::function<void(ImGuiDockNodeFlags)> ImGuiSystem::s_SetDockSpace;

    void ImGuiSystem::initImGui(const rhi::RenderDevice& /*rd*/,
                                const rhi::Swapchain& /*swapchain*/,
                                const os::Window&                       window,
                                const bool                              enableMultiviewport,
                                const bool                              enableDocking,
                                const std::string&                      writableRoot,
                                const char*                             imguiIniFile,
                                std::function<void(ImGuiDockNodeFlags)> setDockSpace)
    {
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
        ImGuiGraphNode::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

#ifdef IMGUI_HAS_DOCK
        if (enableDocking)
        {
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            // = true, fixes ugly jittering (still present in ImGui 1.89.7).
            io.ConfigDockingTransparentPayload = true;
            io.ConfigDockingWithShift          = true;
        }
#endif
#ifdef IMGUI_HAS_VIEWPORT
        if (enableMultiviewport)
        {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        }
#endif
        io.IniFilename                 = nullptr; // Disable automatic .ini saving, we will handle it ourselves.
        const std::string imguiIniPath = get_imgui_config_file_full_path(writableRoot, imguiIniFile);
        if (!imguiIniPath.empty())
        {
            ImGui::LoadIniSettingsFromDisk(imguiIniPath.c_str());
        }

        s_SetDockSpace = setDockSpace;

#ifdef IMGUI_HAS_VIEWPORT
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            auto& style                       = ImGui::GetStyle();
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
#endif

        // Add TTF Fonts & Icon Fonts
        ImFontConfig fontConfig {};
        fontConfig.MergeMode   = false;
        fontConfig.PixelSnapH  = true;
        fontConfig.OversampleH = fontConfig.OversampleV = 1;
        fontConfig.GlyphMinAdvanceX                     = 4.0f;
        fontConfig.SizePixels                           = 12.0f;

        static const ImWchar ranges[] = {
            0x0020,
            0x00FF,
            0x0400,
            0x044F,
            0,
        };

        const float fontSize = 16.0f;

        // Roboto
        io.Fonts->AddFontFromMemoryCompressedTTF(
            RobotoRegular_compressed_data, RobotoRegular_compressed_size, fontSize, &fontConfig, ranges);

        // https://github.com/ocornut/imgui/issues/3247
        static const ImWchar iconsRanges[] = {ICON_MIN_MDI, ICON_MAX_MDI, 0};
        ImFontConfig         iconsConfig {};
        iconsConfig.MergeMode  = true;
        iconsConfig.PixelSnapH = true;
        addCompressedFontTTF(io,
                             materialdesignicons_webfont_ttf_lz4,
                             static_cast<int>(materialdesignicons_webfont_ttf_lz4_size),
                             static_cast<int>(materialdesignicons_webfont_ttf_size),
                             16.0f,
                             &iconsConfig,
                             iconsRanges);

        // Colour emoji folded into the default font (after icons, before the other faces) so chat
        // and UI text render emoji rather than tofu. Requires the FreeType-enabled imgui package.
        tryMergeColorEmoji(io, 16.0f);

        // Simplified-Chinese glyphs folded into the default font so CJK text (UI + AI chat) renders.
        // WenQuanYi Micro Hei is a compact smooth hei-ti (GB2312 subset ~1.3MB) that harmonises with
        // Roboto. No glyph ranges needed (1.92 loads glyphs on demand).
        {
            ImFontConfig cnConfig {};
            cnConfig.MergeMode = true;
            addCompressedFontTTF(io,
                                 wqy_microhei_ttf_lz4,
                                 static_cast<int>(wqy_microhei_ttf_lz4_size),
                                 static_cast<int>(wqy_microhei_ttf_size),
                                 fontSize,
                                 &cnConfig);
        }

        io.Fonts->AddFontFromMemoryCompressedTTF(
            RobotoBold_compressed_data, RobotoBold_compressed_size, fontSize + 2.0f, &fontConfig, ranges);

        io.Fonts->AddFontFromMemoryCompressedTTF(
            RobotoRegular_compressed_data, RobotoRegular_compressed_size, fontSize * 0.8f, &fontConfig, ranges);

        ImFontConfig codeFontConfig     = fontConfig;
        codeFontConfig.GlyphMinAdvanceX = 0.0f;
        io.Fonts->AddFontFromMemoryCompressedTTF(
            CousineRegular_compressed_data, CousineRegular_compressed_size, fontSize, &codeFontConfig, ranges);

        setImGuiStyle();

        // Keep window/display scale semantic intact in window backends.
        // For ImGui style sizing, only apply density scaling on Android.
        float displayScale = window.getDisplayScale();
        if (displayScale <= 0.0f)
            displayScale = 1.0f;
#if !defined(__ANDROID__)
        displayScale = 1.0f;
#endif
        auto& style = ImGui::GetStyle();
        style.ScaleAllSizes(displayScale);
        style.FontScaleDpi = displayScale;
    }

    void ImGuiSystem::shutdownImGui(const std::string& writableRoot, const char* imguiIniFile)
    {
        // Before shutting down, save .ini settings to disk.
        const std::string imguiIniPath = get_imgui_config_file_full_path(writableRoot, imguiIniFile);
        if (!imguiIniPath.empty())
        {
            ImGui::SaveIniSettingsToDisk(imguiIniPath.c_str());
        }

        ImGuiGraphNode::DestroyContext();
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }

    void ImGuiSystem::setImGuiStyle()
    {
        // Vultra editor theme baseline.
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::StyleColorsDark(&style);

        style.Alpha                            = 1.0f;
        style.DisabledAlpha                    = 0.6000000238418579f;
        style.WindowPadding                    = ImVec2(8.0f, 8.0f);
        style.WindowRounding                   = 0.0f;
        style.WindowBorderSize                 = 1.0f;
        style.WindowMinSize                    = ImVec2(32.0f, 32.0f);
        style.WindowTitleAlign                 = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition         = ImGuiDir_Left;
        style.ChildRounding                    = 0.0f;
        style.ChildBorderSize                  = 1.0f;
        style.PopupRounding                    = 0.0f;
        style.PopupBorderSize                  = 1.0f;
        style.FramePadding                     = ImVec2(4.0f, 3.0f);
        style.FrameRounding                    = 0.0f;
        style.FrameBorderSize                  = 0.0f;
        style.ItemSpacing                      = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing                 = ImVec2(4.0f, 4.0f);
        style.CellPadding                      = ImVec2(4.0f, 2.0f);
        style.IndentSpacing                    = 21.0f;
        style.ColumnsMinSpacing                = 6.0f;
        style.ScrollbarSize                    = 14.0f;
        style.ScrollbarRounding                = 9.0f;
        style.GrabMinSize                      = 10.0f;
        style.GrabRounding                     = 0.0f;
        style.TabRounding                      = 4.0f;
        style.TabBorderSize                    = 0.0f;
        style.TabCloseButtonMinWidthSelected   = 0.0f;
        style.TabCloseButtonMinWidthUnselected = 0.0f;
        style.ColorButtonPosition              = ImGuiDir_Right;
        style.ButtonTextAlign                  = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign              = ImVec2(0.0f, 0.0f);

        style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] =
            ImVec4(0.4980392158031464f, 0.4980392158031464f, 0.4980392158031464f, 1.0f);
        style.Colors[ImGuiCol_WindowBg] =
            ImVec4(0.05882352963089943f, 0.05882352963089943f, 0.05882352963089943f, 0.9399999976158142f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
        style.Colors[ImGuiCol_PopupBg] =
            ImVec4(0.0784313753247261f, 0.0784313753247261f, 0.0784313753247261f, 0.9399999976158142f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
        style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        style.Colors[ImGuiCol_FrameBg] =
            ImVec4(0.2000000029802322f, 0.2078431397676468f, 0.2196078449487686f, 0.5400000214576721f);
        style.Colors[ImGuiCol_FrameBgHovered] =
            ImVec4(0.4000000059604645f, 0.4000000059604645f, 0.4000000059604645f, 0.4000000059604645f);
        style.Colors[ImGuiCol_FrameBgActive] =
            ImVec4(0.1764705926179886f, 0.1764705926179886f, 0.1764705926179886f, 0.6700000166893005f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.03921568766236305f, 0.03921568766236305f, 0.03921568766236305f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] =
            ImVec4(0.2862745225429535f, 0.2862745225429535f, 0.2862745225429535f, 1.0f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.5099999904632568f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarBg] =
            ImVec4(0.01960784383118153f, 0.01960784383118153f, 0.01960784383118153f, 0.5299999713897705f);
        style.Colors[ImGuiCol_ScrollbarGrab] =
            ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] =
            ImVec4(0.407843142747879f, 0.407843142747879f, 0.407843142747879f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] =
            ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
        style.Colors[ImGuiCol_CheckMark]  = ImVec4(0.9372549057006836f, 0.9372549057006836f, 0.9372549057006836f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] =
            ImVec4(0.8588235378265381f, 0.8588235378265381f, 0.8588235378265381f, 1.0f);
        style.Colors[ImGuiCol_Button] =
            ImVec4(0.4392156898975372f, 0.4392156898975372f, 0.4392156898975372f, 0.4000000059604645f);
        style.Colors[ImGuiCol_ButtonHovered] =
            ImVec4(0.4588235318660736f, 0.4666666686534882f, 0.47843137383461f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] =
            ImVec4(0.4196078479290009f, 0.4196078479290009f, 0.4196078479290009f, 1.0f);
        style.Colors[ImGuiCol_Header] =
            ImVec4(0.6980392336845398f, 0.6980392336845398f, 0.6980392336845398f, 0.3100000023841858f);
        style.Colors[ImGuiCol_HeaderHovered] =
            ImVec4(0.6980392336845398f, 0.6980392336845398f, 0.6980392336845398f, 0.800000011920929f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.47843137383461f, 0.4980392158031464f, 0.5176470875740051f, 1.0f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
        style.Colors[ImGuiCol_SeparatorHovered] =
            ImVec4(0.7176470756530762f, 0.7176470756530762f, 0.7176470756530762f, 0.7799999713897705f);
        style.Colors[ImGuiCol_SeparatorActive] =
            ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
        style.Colors[ImGuiCol_ResizeGrip] =
            ImVec4(0.9098039269447327f, 0.9098039269447327f, 0.9098039269447327f, 0.25f);
        style.Colors[ImGuiCol_ResizeGripHovered] =
            ImVec4(0.8078431487083435f, 0.8078431487083435f, 0.8078431487083435f, 0.6700000166893005f);
        style.Colors[ImGuiCol_ResizeGripActive] =
            ImVec4(0.4588235318660736f, 0.4588235318660736f, 0.4588235318660736f, 0.949999988079071f);
        style.Colors[ImGuiCol_Tab] =
            ImVec4(0.1764705926179886f, 0.3490196168422699f, 0.5764706134796143f, 0.8619999885559082f);
        style.Colors[ImGuiCol_TabHovered] =
            ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.196078434586525f, 0.407843142747879f, 0.6784313917160034f, 1.0f);
        style.Colors[ImGuiCol_TabUnfocused] =
            ImVec4(0.06666667014360428f, 0.1019607856869698f, 0.1450980454683304f, 0.9724000096321106f);
        style.Colors[ImGuiCol_TabUnfocusedActive] =
            ImVec4(0.1333333402872086f, 0.2588235437870026f, 0.4235294163227081f, 1.0f);
        style.Colors[ImGuiCol_PlotLines] = ImVec4(0.6078431606292725f, 0.6078431606292725f, 0.6078431606292725f, 1.0f);
        style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.4274509847164154f, 0.3490196168422699f, 1.0f);
        style.Colors[ImGuiCol_PlotHistogram] =
            ImVec4(0.729411780834198f, 0.6000000238418579f, 0.1490196138620377f, 1.0f);
        style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6000000238418579f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_TableHeaderBg] =
            ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
        style.Colors[ImGuiCol_TableBorderStrong] =
            ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
        style.Colors[ImGuiCol_TableBorderLight] =
            ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
        style.Colors[ImGuiCol_TableRowBg]    = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
        style.Colors[ImGuiCol_TextSelectedBg] =
            ImVec4(0.8666666746139526f, 0.8666666746139526f, 0.8666666746139526f, 0.3499999940395355f);
        style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
        style.Colors[ImGuiCol_NavHighlight] =
            ImVec4(0.6000000238418579f, 0.6000000238418579f, 0.6000000238418579f, 1.0f);
        style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
        style.Colors[ImGuiCol_NavWindowingDimBg] =
            ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
        style.Colors[ImGuiCol_ModalWindowDimBg] =
            ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.3499999940395355f);

        ImGui::StyleColorsDark(&style);

        // Vultra editor theme: compact dark UI shared by editor, launcher and popups.
        style.WindowPadding     = ImVec2(7.0f, 6.0f);
        style.FramePadding      = ImVec2(7.0f, 4.0f);
        style.CellPadding       = ImVec2(6.0f, 4.0f);
        style.ItemSpacing       = ImVec2(7.0f, 5.0f);
        style.ItemInnerSpacing  = ImVec2(5.0f, 4.0f);
        style.WindowRounding    = 3.0f;
        style.ChildRounding     = 4.0f;
        style.PopupRounding     = 4.0f;
        style.FrameRounding     = 4.0f;
        style.GrabRounding      = 4.0f;
        style.TabRounding       = 4.0f;
        style.ScrollbarRounding = 6.0f;
        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FrameBorderSize   = 0.0f;
        style.TabBorderSize     = 0.0f;
        style.ScrollbarSize     = 12.0f;
        style.IndentSpacing     = 18.0f;

        auto& c                             = style.Colors;
        namespace theme                     = vultra::imgui_theme;
        c[ImGuiCol_Text]                    = theme::text();
        c[ImGuiCol_TextDisabled]            = theme::textMuted();
        c[ImGuiCol_WindowBg]                = theme::backgroundTransparent(0.985f);
        c[ImGuiCol_ChildBg]                 = theme::backgroundTransparent(0.965f);
        c[ImGuiCol_PopupBg]                 = theme::backgroundTransparent(0.985f);
        c[ImGuiCol_Border]                  = theme::border();
        c[ImGuiCol_Border].w                = 0.88f;
        c[ImGuiCol_BorderShadow]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_FrameBg]                 = theme::frame();
        c[ImGuiCol_FrameBg].w               = 0.96f;
        c[ImGuiCol_FrameBgHovered]          = theme::frameHovered();
        c[ImGuiCol_FrameBgActive]           = theme::frameActive();
        c[ImGuiCol_TitleBg]                 = theme::backgroundDeep();
        c[ImGuiCol_TitleBgActive]           = theme::panel();
        c[ImGuiCol_TitleBgCollapsed]        = theme::backgroundDeeper();
        c[ImGuiCol_TitleBgCollapsed].w      = 0.95f;
        c[ImGuiCol_MenuBarBg]               = theme::backgroundDeep();
        c[ImGuiCol_ScrollbarBg]             = ImVec4(0.035f, 0.045f, 0.058f, 0.70f);
        c[ImGuiCol_ScrollbarGrab]           = ImVec4(0.125f, 0.155f, 0.195f, 0.95f);
        c[ImGuiCol_ScrollbarGrabHovered]    = ImVec4(0.195f, 0.245f, 0.305f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive]     = ImVec4(0.230f, 0.300f, 0.375f, 1.00f);
        c[ImGuiCol_CheckMark]               = theme::accent();
        c[ImGuiCol_SliderGrab]              = ImVec4(0.32f, 0.60f, 0.88f, 1.00f);
        c[ImGuiCol_SliderGrabActive]        = ImVec4(0.43f, 0.78f, 1.00f, 1.00f);
        c[ImGuiCol_Button]                  = theme::buttonTransparent(0.96f);
        c[ImGuiCol_ButtonHovered]           = theme::buttonHovered();
        c[ImGuiCol_ButtonActive]            = theme::accentButton();
        c[ImGuiCol_Header]                  = theme::header();
        c[ImGuiCol_Header].w                = 0.88f;
        c[ImGuiCol_HeaderHovered]           = theme::headerHovered();
        c[ImGuiCol_HeaderHovered].w         = 0.96f;
        c[ImGuiCol_HeaderActive]            = theme::headerActive();
        c[ImGuiCol_Separator]               = theme::separator();
        c[ImGuiCol_Separator].w             = 0.95f;
        c[ImGuiCol_SeparatorHovered]        = ImVec4(0.180f, 0.420f, 0.660f, 1.00f);
        c[ImGuiCol_SeparatorActive]         = ImVec4(0.220f, 0.560f, 0.850f, 1.00f);
        c[ImGuiCol_ResizeGrip]              = ImVec4(0.160f, 0.280f, 0.380f, 0.30f);
        c[ImGuiCol_ResizeGripHovered]       = ImVec4(0.230f, 0.500f, 0.720f, 0.65f);
        c[ImGuiCol_ResizeGripActive]        = ImVec4(0.270f, 0.670f, 0.950f, 0.95f);
        c[ImGuiCol_Tab]                     = theme::panel();
        c[ImGuiCol_TabHovered]              = ImVec4(0.105f, 0.230f, 0.350f, 1.00f);
        c[ImGuiCol_TabActive]               = ImVec4(0.085f, 0.125f, 0.165f, 1.00f);
        c[ImGuiCol_TabUnfocused]            = theme::background();
        c[ImGuiCol_TabUnfocusedActive]      = theme::frameHovered();
        c[ImGuiCol_TableHeaderBg]           = ImVec4(0.080f, 0.100f, 0.128f, 1.00f);
        c[ImGuiCol_TableBorderStrong]       = ImVec4(0.150f, 0.185f, 0.230f, 1.00f);
        c[ImGuiCol_TableBorderLight]        = ImVec4(0.105f, 0.130f, 0.165f, 1.00f);
        c[ImGuiCol_TableRowBg]              = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_TableRowBgAlt]           = ImVec4(1.00f, 1.00f, 1.00f, 0.025f);
        c[ImGuiCol_PlotLines]               = ImVec4(0.36f, 0.66f, 0.90f, 1.00f);
        c[ImGuiCol_PlotLinesHovered]        = ImVec4(0.43f, 0.78f, 1.00f, 1.00f);
        c[ImGuiCol_PlotHistogram]           = ImVec4(0.25f, 0.56f, 0.82f, 1.00f);
        c[ImGuiCol_PlotHistogramHovered]    = ImVec4(0.36f, 0.78f, 1.00f, 1.00f);
        c[ImGuiCol_TextSelectedBg]          = ImVec4(0.110f, 0.380f, 0.660f, 0.55f);
        c[ImGuiCol_DragDropTarget]          = theme::accent();
        c[ImGuiCol_DragDropTarget].w        = 0.90f;
        c[ImGuiCol_NavHighlight]            = theme::accent();
        c[ImGuiCol_NavHighlight].w          = 0.90f;
        c[ImGuiCol_NavWindowingHighlight]   = theme::accent();
        c[ImGuiCol_NavWindowingHighlight].w = 0.72f;
        c[ImGuiCol_NavWindowingDimBg]       = ImVec4(0.01f, 0.015f, 0.020f, 0.35f);
        c[ImGuiCol_ModalWindowDimBg]        = theme::dim();
#ifdef IMGUI_HAS_DOCK
        c[ImGuiCol_DockingPreview] = ImVec4(0.20f, 0.62f, 1.00f, 0.62f);
        c[ImGuiCol_DockingEmptyBg] = ImVec4(0.040f, 0.050f, 0.064f, 1.00f);
#endif
    }
} // namespace vultra
