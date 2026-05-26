#pragma once

#include "editor_app/editor_context.hpp"

#include <string>

namespace vultra_app
{
    class EditorWindow
    {
    public:
        explicit EditorWindow(std::string name, std::string icon = {}) :
            m_Name(std::move(name)), m_Icon(std::move(icon))
        {
            if (m_Icon.empty())
                m_DisplayName = m_Name;
            else
                m_DisplayName = m_Icon + "  " + m_Name;
            m_Title = m_DisplayName + "###" + m_Name;
        }
        virtual ~EditorWindow() = default;

        virtual void tick(EditorContext& /*ctx*/) {}
        virtual void draw(EditorContext& ctx) = 0;
        virtual void onClosed(EditorContext& /*ctx*/) {}
        virtual void onDestroy(EditorContext& /*ctx*/) {}

        [[nodiscard]] const std::string& name() const { return m_Name; }
        [[nodiscard]] const std::string& displayName() const { return m_DisplayName; }
        [[nodiscard]] const std::string& title() const { return m_Title; }
        [[nodiscard]] bool&              open() { return m_Open; }

    protected:
        std::string m_Name;
        std::string m_Icon;
        std::string m_DisplayName;
        std::string m_Title;
        bool        m_Open {true};
    };
} // namespace vultra_app
