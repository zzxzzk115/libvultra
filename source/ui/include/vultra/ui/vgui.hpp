#pragma once

#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/platform/os/window.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace vultra
{
    // Retained, authored game UI. Render and input use VRI and Window; RmlUi stays behind this boundary.
    class VGui
    {
    public:
        VGui(Device& device, Window& window, VriFormat targetFormat);
        ~VGui();
        VGui(const VGui&)            = delete;
        VGui& operator=(const VGui&) = delete;

        void         loadFont(const std::filesystem::path& path);
        void         loadDocument(const std::filesystem::path& path);
        void         bindClick(std::string_view elementId, std::function<void()> callback);
        void         bindChange(std::string_view elementId, std::function<void()> callback);
        std::string  value(std::string_view elementId) const;
        void         setValue(std::string_view elementId, std::string_view value);
        bool         isChecked(std::string_view elementId) const;
        void         setText(std::string_view elementId, std::string_view text);
        void         setChecked(std::string_view elementId, bool checked);
        void         update(const Input& input, Extent framebuffer);
        void         draw(VriCommandBuffer* cmd, Texture& target);
        InputCapture inputCapture() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
