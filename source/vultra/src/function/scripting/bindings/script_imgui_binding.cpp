#include "vultra/function/scripting/bindings/script_imgui_binding.hpp"

// NOLINTBEGIN
#include <imgui.h>
#include <imgui_internal.h>
// NOLINTEND

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <tuple>

namespace vultra
{
    namespace imgui_lua_detail
    {
        void ensureFrame()
        {
            ImGuiContext* context = ImGui::GetCurrentContext();
            if (!context || !context->WithinFrameScope)
                throw std::runtime_error("ImGui.* functions may only be called during the ImGui frame "
                                         "(between NewFrame and EndFrame)");
        }
    } // namespace imgui_lua_detail

    void registerScriptImGuiBindings(sol::state& lua, ScriptContext& ctx)
    {
        if (!ctx.imguiService)
            return;

        registerGeneratedImGuiBindings(lua);

        sol::table imgui = lua["ImGui"];

        // Hand-written irregular cases (buffer-based input) -- Lua strings are
        // immutable, so the buffer round-trips through a fixed-size scratch.
        imgui.set_function("InputText",
                           [](const char* label, std::string text, sol::optional<lua_Integer> flags) {
                               imgui_lua_detail::ensureFrame();
                               std::array<char, 1024> buffer {};
                               const size_t           len = std::min(text.size(), buffer.size() - 1);
                               std::memcpy(buffer.data(), text.data(), len);
                               const bool changed =
                                   ImGui::InputText(label,
                                                    buffer.data(),
                                                    buffer.size(),
                                                    static_cast<ImGuiInputTextFlags>(flags.value_or(0)));
                               return std::make_tuple(changed, std::string(buffer.data()));
                           });

        imgui.set_function("InputTextMultiline",
                           [](const char* label, std::string text, sol::optional<lua_Integer> flags) {
                               imgui_lua_detail::ensureFrame();
                               std::array<char, 4096> buffer {};
                               const size_t           len = std::min(text.size(), buffer.size() - 1);
                               std::memcpy(buffer.data(), text.data(), len);
                               const bool changed =
                                   ImGui::InputTextMultiline(label,
                                                             buffer.data(),
                                                             buffer.size(),
                                                             ImVec2(0, 0),
                                                             static_cast<ImGuiInputTextFlags>(flags.value_or(0)));
                               return std::make_tuple(changed, std::string(buffer.data()));
                           });
    }
} // namespace vultra
