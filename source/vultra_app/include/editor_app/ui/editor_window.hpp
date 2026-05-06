#pragma once

#include "editor_app/editor_context.hpp"

#include <string>

namespace vultra_app
{
    class EditorWindow
    {
    public:
        explicit EditorWindow(std::string name) : m_Name(std::move(name)) {}
        virtual ~EditorWindow() = default;

        virtual void draw(EditorContext& ctx) = 0;
        virtual void onClosed(EditorContext& /*ctx*/) {}
        virtual void onDestroy(EditorContext& /*ctx*/) {}

        [[nodiscard]] const std::string& name() const { return m_Name; }
        [[nodiscard]] bool&              open() { return m_Open; }

    protected:
        std::string m_Name;
        bool        m_Open {true};
    };
} // namespace vultra_app
