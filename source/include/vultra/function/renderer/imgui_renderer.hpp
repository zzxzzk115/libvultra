#pragma once

#include "vultra/core/os/window.hpp"

// NOLINTBEGIN
#include <imgui.h>
#include <magic_enum/magic_enum.hpp>
// NOLINTEND

#include <functional>
#include <future>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        class Swapchain;
        struct FramebufferInfo;
        class CommandBuffer;
        class Texture;
    } // namespace rhi

    namespace imgui
    {
        class ImGuiRenderer
        {
        public:
            static void initImGui(const rhi::RenderDevice&,
                                  const rhi::Swapchain&,
                                  const os::Window&,
                                  bool                                    enableMultiviewport,
                                  bool                                    enableDocking,
                                  const char*                             imguiIniFile,
                                  std::function<void(ImGuiDockNodeFlags)> setDockSpace = nullptr);

            static void processEvent(const os::GeneralWindowEvent& event);
            static void begin();
            static void render(rhi::CommandBuffer&, const rhi::FramebufferInfo&);
            static void end();
            static void postRender();
            static void shutdown();

        private:
            static std::function<void(ImGuiDockNodeFlags)> s_SetDockSpace;
        };

        using ImGuiTextureID = VkDescriptorSet;

        ImGuiTextureID addTexture(const rhi::Texture& texture);
        ImGuiTextureID addTexture(const rhi::Texture& texture, const vk::Sampler& sampler);

        void removeTexture(rhi::RenderDevice& rd, ImGuiTextureID& textureID);

        void textureViewer(const std::string_view title,
                           ImGuiTextureID         textureID,
                           const rhi::Texture&    texture,
                           const ImVec2&          textureSize,
                           const std::string_view filePath,
                           rhi::RenderDevice&     rd,
                           bool                   open);
    } // namespace imgui

    namespace ImGuiExt
    {
        template<typename T>
        inline void ComboFlags(const char* title, T& flags)
        {
            std::string comboLabel;
            for (int i = 0; i < magic_enum::enum_count<T>(); ++i)
            {
                auto flag = static_cast<T>(1 << i);
                if ((flags & flag) == flag)
                {
                    if (!comboLabel.empty())
                        comboLabel += " | ";
                    comboLabel += magic_enum::enum_name(flag).data();
                }
            }
            if (comboLabel.empty())
                comboLabel = "None";

            if (ImGui::BeginCombo(title, comboLabel.c_str()))
            {
                for (int i = 0; i < magic_enum::enum_count<T>(); ++i)
                {
                    auto flag = static_cast<T>(1 << i);
                    ImGui::CheckboxFlags(magic_enum::enum_name(flag).data(),
                                         reinterpret_cast<unsigned int*>(&flags),
                                         static_cast<unsigned int>(flag));
                }
                ImGui::EndCombo();
            }
        }

        template<typename T>
        inline void Combo(const char* title, T& value)
        {
            auto enumValues = magic_enum::enum_names<T>();
            int  enumIndex  = static_cast<int>(value);
            if (ImGui::BeginCombo(title, enumValues[enumIndex].data()))
            {
                for (int n = 0; n < enumValues.size(); n++)
                {
                    const bool selected = (enumIndex == n);
                    if (ImGui::Selectable(enumValues[n].data(), selected))
                    {
                        value = magic_enum::enum_cast<T>(enumValues[n]).value();
                    }
                }
                ImGui::EndCombo();
            }
        }

        template<typename T>
        inline void Combo(const char* title, T& value, const std::string& excludePrefix)
        {
            auto enumValues = magic_enum::enum_names<T>();
            int  enumIndex  = static_cast<int>(value);
            if (ImGui::BeginCombo(title, enumValues[enumIndex].data()))
            {
                for (int n = 0; n < enumValues.size(); n++)
                {
                    const bool selected = (enumIndex == n);

                    // Skip values with the specified prefix
                    if (enumValues[n].starts_with(excludePrefix))
                        continue;

                    if (ImGui::Selectable(enumValues[n].data(), selected))
                    {
                        value = magic_enum::enum_cast<T>(enumValues[n]).value();
                    }
                }
                ImGui::EndCombo();
            }
        }

        void DrawVec3Control(const std::string& label,
                             glm::vec3&         values,
                             float              resetValue  = 0.0f,
                             float              columnWidth = 80.0f);

        class RenamePopupWidget
        {
        public:
            RenamePopupWidget() = default;

            void open(const char* currentName);
            void close();

            void setRenameCallback(std::function<void(const char*)> callback);

            void onImGui(const char* title);

        private:
            char                             m_RenameBuffer[256] {0};
            bool                             m_IsOpen {false};
            bool                             m_RequestOpen {false};
            bool                             m_IsFirstFrame {true};
            std::function<void(const char*)> m_RenameCallback;
        };

        class AsyncProgressWidget
        {
        public:
            AsyncProgressWidget() = default;

            void open(const char* message);
            void close();

            void setFuture(std::future<void>&& future);
            void setFinishedCallback(std::function<void()> callback);

            void onImGui(const char* title, const char* overlay);

        private:
            std::string m_Message;
            bool        m_IsOpen {false};
            bool        m_RequestOpen {false};
            bool        m_IsFirstFrame {true};

            std::future<void>     m_Future;
            std::function<void()> m_FinishedCallback;
        };
    } // namespace ImGuiExt
} // namespace vultra