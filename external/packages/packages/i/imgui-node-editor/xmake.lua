package("imgui-node-editor")
    set_homepage("https://github.com/thedmd/imgui-node-editor")
    set_description("Dear ImGui node canvas for the research editor")
    set_license("MIT")
    add_urls("https://github.com/thedmd/imgui-node-editor/archive/$(version).tar.gz")
    add_versions("021aa0ea4da13fed864bafb2a92d4c5205076866",
                 "7b51a14f5e13572b4fe60c9bdba536d7dd164ba8821e1539140f38cd29aaff14")
    add_deps("imgui")
    on_install("windows", "linux", "macosx", function (package)
        io.writefile("xmake.lua", [[
            add_requires("imgui")
            target("imgui-node-editor")
                set_kind("static")
                set_languages("c++17")
                add_files("imgui_node_editor.cpp", "imgui_node_editor_api.cpp", "imgui_canvas.cpp", "crude_json.cpp")
                add_headerfiles("imgui_node_editor.h")
                add_packages("imgui")
        ]])
        import("package.tools.xmake").install(package)
    end)
    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <imgui_node_editor.h>
            void test() {
                auto* editor = ax::NodeEditor::CreateEditor();
                ax::NodeEditor::DestroyEditor(editor);
            }
        ]]}))
    end)
package_end()
