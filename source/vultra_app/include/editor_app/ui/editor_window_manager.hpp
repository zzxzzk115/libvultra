#pragma once

#include "editor_app/ui/editor_window.hpp"

#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

namespace vultra_app
{
    class EditorWindowManager
    {
    public:
        template<typename T, typename... Args>
        T& addWindow(Args&&... args)
        {
            static_assert(std::is_base_of_v<EditorWindow, T>);
            auto window = std::make_unique<T>(std::forward<Args>(args)...);
            auto& ref   = *window;
            m_Windows.push_back(std::move(window));
            return ref;
        }

        void draw(EditorContext& ctx);
        void tick(EditorContext& ctx);
        void destroy(EditorContext& ctx);
        bool saveSceneThumbnail(EditorContext& ctx, std::string_view sceneUri);

        [[nodiscard]] const std::vector<std::unique_ptr<EditorWindow>>& windows() const { return m_Windows; }

    private:
        std::vector<std::unique_ptr<EditorWindow>> m_Windows;
        std::vector<bool>                          m_WasOpen;
        std::string                                m_LastLanguage; // drives title refresh on switch
    };
} // namespace vultra_app
