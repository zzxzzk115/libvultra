#pragma once

#include "editor_app/editor_context.hpp"

#include <vultra/core/i18n/i18n.hpp>

#include <string>

namespace vultra_app
{
    class EditorWindow
    {
    public:
        // `name` is the stable, language-invariant identity (used as the imgui "###id" and for
        // window lookups). `titleKey` is the i18n key for the visible title; when empty the title
        // falls back to `name`. refreshLocalization() recomputes the visible strings for the current
        // language while keeping the id stable, so docking/imgui.ini survive a language switch.
        explicit EditorWindow(std::string name, std::string icon = {}, std::string titleKey = {}) :
            m_Name(std::move(name)), m_Icon(std::move(icon)), m_TitleKey(std::move(titleKey))
        {
            refreshLocalization();
        }
        virtual ~EditorWindow() = default;

        virtual void tick(EditorContext& /*ctx*/) {}
        virtual void draw(EditorContext& ctx) = 0;
        virtual void onClosed(EditorContext& /*ctx*/) {}
        virtual void onDestroy(EditorContext& /*ctx*/) {}

        // Rebuild the visible title from the active language. Called once at construction and again by
        // the window manager whenever the language changes. Virtual so windows with a non-i18n literal
        // title (e.g. Lua-scripted panels) can keep their own label across a language switch.
        virtual void refreshLocalization()
        {
            const std::string label = m_TitleKey.empty() ? m_Name : std::string {vultra::tr(m_TitleKey)};
            m_DisplayName           = m_Icon.empty() ? label : m_Icon + "  " + label;
            m_Title                 = m_DisplayName + "###" + m_Name;
        }

        [[nodiscard]] const std::string& name() const { return m_Name; }
        [[nodiscard]] const std::string& displayName() const { return m_DisplayName; }
        [[nodiscard]] const std::string& title() const { return m_Title; }
        [[nodiscard]] bool&              open() { return m_Open; }

    protected:
        std::string m_Name;
        std::string m_Icon;
        std::string m_TitleKey;
        std::string m_DisplayName;
        std::string m_Title;
        bool        m_Open {true};
    };
} // namespace vultra_app
