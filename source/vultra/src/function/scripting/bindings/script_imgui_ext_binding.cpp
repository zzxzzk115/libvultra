#include "vultra/function/scripting/bindings/script_imgui_binding.hpp"

// imgui-ext extension bindings (hand-written -- these libraries have no
// dear_bindings metadata). Each extension is exposed under its own table,
// keeping upstream PascalCase names (the same spec exception ImGui uses,
// doc/lua_api_design.md section 9). Matrices are Lua arrays of 16 numbers
// (column-major, as the extensions expect); vec3s are arrays of 3.

// NOLINTBEGIN
#include <imgui.h>
#include <imgui_internal.h>

#include <ImGuiFileDialog/ImGuiFileDialog.h>
#include <ImGuizmo/ImGuizmo.h>
#include <imnodes/imnodes.h>
#include <imoguizmo/imoguizmo.hpp>
#include <implot/implot.h>
// NOLINTEND

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace vultra
{
    namespace
    {
        std::array<float, 16> readMat16(const sol::table& t)
        {
            std::array<float, 16> m {};
            for (int i = 0; i < 16; ++i)
                m[static_cast<size_t>(i)] = t.get_or(i + 1, 0.0f);
            return m;
        }
        sol::table makeMat16(sol::state_view lua, const float* m)
        {
            auto t = lua.create_table(16, 0);
            for (int i = 0; i < 16; ++i)
                t[i + 1] = m[i];
            return t;
        }
        std::array<float, 3> readVec3(const sol::table& t)
        {
            return {t.get_or(1, 0.0f), t.get_or(2, 0.0f), t.get_or(3, 0.0f)};
        }
        sol::table makeVec3(sol::state_view lua, const float* v)
        {
            auto t = lua.create_table(3, 0);
            t[1] = v[0];
            t[2] = v[1];
            t[3] = v[2];
            return t;
        }
        std::vector<double> readDoubles(const sol::table& t)
        {
            std::vector<double> out;
            out.reserve(t.size());
            for (std::size_t i = 1; i <= t.size(); ++i)
                out.push_back(t.get_or(i, 0.0));
            return out;
        }
    } // namespace

    void registerImGuiExtBindings(sol::state& lua)
    {
        // ---- ImGuizmo: 3D transform gizmos --------------------------------
        {
            auto gizmo = lua.create_named_table("ImGuizmo");

            gizmo.set_function("BeginFrame", [] {
                imgui_lua_detail::ensureFrame();
                ImGuizmo::BeginFrame();
            });
            gizmo.set_function("Enable", [](bool enable) { ImGuizmo::Enable(enable); });
            gizmo.set_function("SetOrthographic", [](bool ortho) { ImGuizmo::SetOrthographic(ortho); });
            gizmo.set_function("SetDrawlist", [] {
                imgui_lua_detail::ensureFrame();
                ImGuizmo::SetDrawlist(nullptr);
            });
            gizmo.set_function("SetRect", [](float x, float y, float width, float height) {
                ImGuizmo::SetRect(x, y, width, height);
            });
            gizmo.set_function("SetID", [](int id) { ImGuizmo::SetID(id); });
            gizmo.set_function("IsOver", [] { return ImGuizmo::IsOver(); });
            gizmo.set_function("IsUsing", [] { return ImGuizmo::IsUsing(); });
            gizmo.set_function("IsUsingAny", [] { return ImGuizmo::IsUsingAny(); });

            // Manipulate(view, projection, operation, mode, matrix[, snap3]) ->
            // changed, newMatrix. matrix is the model matrix being edited.
            gizmo.set_function("Manipulate",
                               [](const sol::table& view, const sol::table& projection, lua_Integer operation,
                                  lua_Integer mode, const sol::table& matrix, sol::optional<sol::table> snap,
                                  sol::this_state ts) {
                                   imgui_lua_detail::ensureFrame();
                                   const auto v = readMat16(view);
                                   const auto p = readMat16(projection);
                                   auto       m = readMat16(matrix);
                                   std::array<float, 3> s {};
                                   const float*         snapPtr = nullptr;
                                   if (snap)
                                   {
                                       s       = readVec3(*snap);
                                       snapPtr = s.data();
                                   }
                                   const bool changed = ImGuizmo::Manipulate(
                                       v.data(), p.data(), static_cast<ImGuizmo::OPERATION>(operation),
                                       static_cast<ImGuizmo::MODE>(mode), m.data(), nullptr, snapPtr);
                                   return std::make_tuple(changed, makeMat16(sol::state_view {ts}, m.data()));
                               });

            // DecomposeMatrixToComponents(matrix) -> translation, rotation(deg), scale (each a vec3).
            gizmo.set_function("DecomposeMatrixToComponents", [](const sol::table& matrix, sol::this_state ts) {
                const auto           m = readMat16(matrix);
                std::array<float, 3> t {}, r {}, s {};
                ImGuizmo::DecomposeMatrixToComponents(m.data(), t.data(), r.data(), s.data());
                sol::state_view lua {ts};
                return std::make_tuple(makeVec3(lua, t.data()), makeVec3(lua, r.data()), makeVec3(lua, s.data()));
            });
            // RecomposeMatrixFromComponents(translation, rotation(deg), scale) -> matrix.
            gizmo.set_function("RecomposeMatrixFromComponents",
                               [](const sol::table& translation, const sol::table& rotation,
                                  const sol::table& scale, sol::this_state ts) {
                                   const auto            t = readVec3(translation);
                                   const auto            r = readVec3(rotation);
                                   const auto            s = readVec3(scale);
                                   std::array<float, 16> m {};
                                   ImGuizmo::RecomposeMatrixFromComponents(t.data(), r.data(), s.data(), m.data());
                                   return makeMat16(sol::state_view {ts}, m.data());
                               });

            // ViewManipulate(view, length, posX, posY, sizeX, sizeY, bgColor) -> newView.
            gizmo.set_function("ViewManipulate",
                               [](const sol::table& view, float length, float posX, float posY, float sizeX,
                                  float sizeY, lua_Integer bgColor, sol::this_state ts) {
                                   imgui_lua_detail::ensureFrame();
                                   auto v = readMat16(view);
                                   ImGuizmo::ViewManipulate(v.data(), length, ImVec2(posX, posY),
                                                            ImVec2(sizeX, sizeY), static_cast<ImU32>(bgColor));
                                   return makeMat16(sol::state_view {ts}, v.data());
                               });

            auto op            = lua.create_table();
            op["TRANSLATE_X"]  = static_cast<int>(ImGuizmo::TRANSLATE_X);
            op["TRANSLATE_Y"]  = static_cast<int>(ImGuizmo::TRANSLATE_Y);
            op["TRANSLATE_Z"]  = static_cast<int>(ImGuizmo::TRANSLATE_Z);
            op["ROTATE_X"]     = static_cast<int>(ImGuizmo::ROTATE_X);
            op["ROTATE_Y"]     = static_cast<int>(ImGuizmo::ROTATE_Y);
            op["ROTATE_Z"]     = static_cast<int>(ImGuizmo::ROTATE_Z);
            op["ROTATE_SCREEN"]= static_cast<int>(ImGuizmo::ROTATE_SCREEN);
            op["SCALE_X"]      = static_cast<int>(ImGuizmo::SCALE_X);
            op["SCALE_Y"]      = static_cast<int>(ImGuizmo::SCALE_Y);
            op["SCALE_Z"]      = static_cast<int>(ImGuizmo::SCALE_Z);
            op["TRANSLATE"]    = static_cast<int>(ImGuizmo::TRANSLATE);
            op["ROTATE"]       = static_cast<int>(ImGuizmo::ROTATE);
            op["SCALE"]        = static_cast<int>(ImGuizmo::SCALE);
            op["SCALEU"]       = static_cast<int>(ImGuizmo::SCALEU);
            op["UNIVERSAL"]    = static_cast<int>(ImGuizmo::UNIVERSAL);
            op["BOUNDS"]       = static_cast<int>(ImGuizmo::BOUNDS);
            gizmo["OPERATION"] = op;

            auto md       = lua.create_table();
            md["LOCAL"]   = static_cast<int>(ImGuizmo::LOCAL);
            md["WORLD"]   = static_cast<int>(ImGuizmo::WORLD);
            gizmo["MODE"] = md;
        }

        // ---- ImOGuizmo: orientation cube ----------------------------------
        {
            auto cube = lua.create_named_table("ImOGuizmo");
            cube.set_function("SetRect", [](float x, float y, float size) { ImOGuizmo::SetRect(x, y, size); });
            cube.set_function("SetDrawList", [] {
                imgui_lua_detail::ensureFrame();
                ImOGuizmo::SetDrawList(nullptr);
            });
            cube.set_function("BeginFrame", [](sol::optional<bool> background) {
                imgui_lua_detail::ensureFrame();
                ImOGuizmo::BeginFrame(background.value_or(false));
            });
            // DrawGizmo(view, projection[, pivotDistance]) -> interacted, newView.
            cube.set_function("DrawGizmo",
                              [](const sol::table& view, const sol::table& projection,
                                 sol::optional<float> pivotDistance, sol::this_state ts) {
                                  imgui_lua_detail::ensureFrame();
                                  auto       v          = readMat16(view);
                                  const auto p          = readMat16(projection);
                                  const bool interacted = ImOGuizmo::DrawGizmo(v.data(), p.data(),
                                                                               pivotDistance.value_or(0.0f));
                                  return std::make_tuple(interacted, makeMat16(sol::state_view {ts}, v.data()));
                              });
        }

        // ---- ImPlot: plotting ---------------------------------------------
        {
            auto plot = lua.create_named_table("ImPlot");

            plot.set_function("BeginPlot",
                              [](const char* title, sol::optional<float> sizeX, sol::optional<float> sizeY,
                                 sol::optional<lua_Integer> flags) {
                                  imgui_lua_detail::ensureFrame();
                                  return ImPlot::BeginPlot(title, ImVec2(sizeX.value_or(-1.0f), sizeY.value_or(0.0f)),
                                                           static_cast<ImPlotFlags>(flags.value_or(0)));
                              });
            plot.set_function("EndPlot", [] {
                imgui_lua_detail::ensureFrame();
                ImPlot::EndPlot();
            });
            plot.set_function("SetupAxes",
                              [](const char* xLabel, const char* yLabel, sol::optional<lua_Integer> xFlags,
                                 sol::optional<lua_Integer> yFlags) {
                                  ImPlot::SetupAxes(xLabel, yLabel, static_cast<ImPlotAxisFlags>(xFlags.value_or(0)),
                                                    static_cast<ImPlotAxisFlags>(yFlags.value_or(0)));
                              });
            plot.set_function("SetupAxesLimits",
                              [](double xMin, double xMax, double yMin, double yMax, sol::optional<lua_Integer> cond) {
                                  ImPlot::SetupAxesLimits(xMin, xMax, yMin, yMax,
                                                          static_cast<ImPlotCond>(cond.value_or(ImPlotCond_Once)));
                              });
            plot.set_function("SetupLegend", [](lua_Integer location, sol::optional<lua_Integer> flags) {
                ImPlot::SetupLegend(static_cast<ImPlotLocation>(location),
                                    static_cast<ImPlotLegendFlags>(flags.value_or(0)));
            });

            // Plot*(label, ys) uses an implicit x = 0,1,2,...; Plot*(label, xs, ys) is explicit.
            plot.set_function("PlotLine", [](const char* label, const sol::table& a, sol::optional<sol::table> b) {
                imgui_lua_detail::ensureFrame();
                if (b)
                {
                    const auto xs = readDoubles(a);
                    const auto ys = readDoubles(*b);
                    ImPlot::PlotLine(label, xs.data(), ys.data(),
                                     static_cast<int>(std::min(xs.size(), ys.size())));
                }
                else
                {
                    const auto vs = readDoubles(a);
                    ImPlot::PlotLine(label, vs.data(), static_cast<int>(vs.size()));
                }
            });
            plot.set_function("PlotScatter", [](const char* label, const sol::table& a, sol::optional<sol::table> b) {
                imgui_lua_detail::ensureFrame();
                if (b)
                {
                    const auto xs = readDoubles(a);
                    const auto ys = readDoubles(*b);
                    ImPlot::PlotScatter(label, xs.data(), ys.data(),
                                        static_cast<int>(std::min(xs.size(), ys.size())));
                }
                else
                {
                    const auto vs = readDoubles(a);
                    ImPlot::PlotScatter(label, vs.data(), static_cast<int>(vs.size()));
                }
            });
            plot.set_function("PlotBars", [](const char* label, const sol::table& a, sol::optional<sol::table> b,
                                             sol::optional<double> barSize) {
                imgui_lua_detail::ensureFrame();
                if (b)
                {
                    const auto xs = readDoubles(a);
                    const auto ys = readDoubles(*b);
                    ImPlot::PlotBars(label, xs.data(), ys.data(), static_cast<int>(std::min(xs.size(), ys.size())),
                                     barSize.value_or(0.67));
                }
                else
                {
                    const auto vs = readDoubles(a);
                    ImPlot::PlotBars(label, vs.data(), static_cast<int>(vs.size()), barSize.value_or(0.67));
                }
            });

            auto axis     = lua.create_table();
            axis["X1"]    = static_cast<int>(ImAxis_X1);
            axis["X2"]    = static_cast<int>(ImAxis_X2);
            axis["X3"]    = static_cast<int>(ImAxis_X3);
            axis["Y1"]    = static_cast<int>(ImAxis_Y1);
            axis["Y2"]    = static_cast<int>(ImAxis_Y2);
            axis["Y3"]    = static_cast<int>(ImAxis_Y3);
            plot["Axis"]  = axis;

            auto flags          = lua.create_table();
            flags["None"]       = static_cast<int>(ImPlotFlags_None);
            flags["NoTitle"]    = static_cast<int>(ImPlotFlags_NoTitle);
            flags["NoLegend"]   = static_cast<int>(ImPlotFlags_NoLegend);
            flags["NoMenus"]    = static_cast<int>(ImPlotFlags_NoMenus);
            flags["NoInputs"]   = static_cast<int>(ImPlotFlags_NoInputs);
            flags["NoBoxSelect"]= static_cast<int>(ImPlotFlags_NoBoxSelect);
            flags["Equal"]      = static_cast<int>(ImPlotFlags_Equal);
            flags["Crosshairs"] = static_cast<int>(ImPlotFlags_Crosshairs);
            flags["CanvasOnly"] = static_cast<int>(ImPlotFlags_CanvasOnly);
            plot["Flags"]       = flags;

            auto axisFlags           = lua.create_table();
            axisFlags["None"]        = static_cast<int>(ImPlotAxisFlags_None);
            axisFlags["NoLabel"]     = static_cast<int>(ImPlotAxisFlags_NoLabel);
            axisFlags["NoGridLines"] = static_cast<int>(ImPlotAxisFlags_NoGridLines);
            axisFlags["NoTickMarks"] = static_cast<int>(ImPlotAxisFlags_NoTickMarks);
            axisFlags["AutoFit"]     = static_cast<int>(ImPlotAxisFlags_AutoFit);
            plot["AxisFlags"]        = axisFlags;

            auto loc          = lua.create_table();
            loc["Center"]     = static_cast<int>(ImPlotLocation_Center);
            loc["North"]      = static_cast<int>(ImPlotLocation_North);
            loc["South"]      = static_cast<int>(ImPlotLocation_South);
            loc["West"]       = static_cast<int>(ImPlotLocation_West);
            loc["East"]       = static_cast<int>(ImPlotLocation_East);
            loc["NorthWest"]  = static_cast<int>(ImPlotLocation_NorthWest);
            loc["NorthEast"]  = static_cast<int>(ImPlotLocation_NorthEast);
            loc["SouthWest"]  = static_cast<int>(ImPlotLocation_SouthWest);
            loc["SouthEast"]  = static_cast<int>(ImPlotLocation_SouthEast);
            plot["Location"]  = loc;
        }

        // ---- ImGuiFileDialog: file/folder picker --------------------------
        {
            auto fd = lua.create_named_table("ImGuiFileDialog");

            // OpenDialog(key, title, filters[, path]). filters: e.g. ".png,.jpg"
            // or "" / nil for a directory picker.
            fd.set_function("OpenDialog", [](const char* key, const char* title, sol::optional<std::string> filters,
                                             sol::optional<std::string> path) {
                IGFD::FileDialogConfig config;
                config.path                  = path.value_or(".");
                const char* filtersPtr       = (filters && !filters->empty()) ? filters->c_str() : nullptr;
                ImGuiFileDialog::Instance()->OpenDialog(key, title, filtersPtr, config);
            });
            // Display(key[, flags]) -> bool (true the frame a result is produced).
            fd.set_function("Display", [](const char* key, sol::optional<lua_Integer> flags) {
                imgui_lua_detail::ensureFrame();
                return ImGuiFileDialog::Instance()->Display(
                    key, static_cast<ImGuiWindowFlags>(flags.value_or(ImGuiWindowFlags_NoCollapse)));
            });
            fd.set_function("IsOk", [] { return ImGuiFileDialog::Instance()->IsOk(); });
            fd.set_function("GetFilePathName", [] { return ImGuiFileDialog::Instance()->GetFilePathName(); });
            fd.set_function("GetCurrentFileName", [] { return ImGuiFileDialog::Instance()->GetCurrentFileName(); });
            fd.set_function("GetCurrentPath", [] { return ImGuiFileDialog::Instance()->GetCurrentPath(); });
            fd.set_function("Close", [] { ImGuiFileDialog::Instance()->Close(); });
            fd.set_function("IsOpened", [](sol::optional<std::string> key) {
                return key ? ImGuiFileDialog::Instance()->IsOpened(*key) : ImGuiFileDialog::Instance()->IsOpened();
            });
            // GetSelection() -> { [fileName] = filePathName, ... }
            fd.set_function("GetSelection", [](sol::this_state ts) {
                sol::state_view lua {ts};
                auto            t   = lua.create_table();
                const auto      sel = ImGuiFileDialog::Instance()->GetSelection();
                for (const auto& [name, fullPath] : sel)
                    t[name] = fullPath;
                return t;
            });
        }

        // ---- ImNodes: node editor -----------------------------------------
        {
            auto nodes = lua.create_named_table("ImNodes");

            nodes.set_function("BeginNodeEditor", [] {
                imgui_lua_detail::ensureFrame();
                ImNodes::BeginNodeEditor();
            });
            nodes.set_function("EndNodeEditor", [] { ImNodes::EndNodeEditor(); });
            nodes.set_function("BeginNode", [](int id) { ImNodes::BeginNode(id); });
            nodes.set_function("EndNode", [] { ImNodes::EndNode(); });
            nodes.set_function("BeginNodeTitleBar", [] { ImNodes::BeginNodeTitleBar(); });
            nodes.set_function("EndNodeTitleBar", [] { ImNodes::EndNodeTitleBar(); });
            nodes.set_function("BeginInputAttribute", [](int id) { ImNodes::BeginInputAttribute(id); });
            nodes.set_function("EndInputAttribute", [] { ImNodes::EndInputAttribute(); });
            nodes.set_function("BeginOutputAttribute", [](int id) { ImNodes::BeginOutputAttribute(id); });
            nodes.set_function("EndOutputAttribute", [] { ImNodes::EndOutputAttribute(); });
            nodes.set_function("BeginStaticAttribute", [](int id) { ImNodes::BeginStaticAttribute(id); });
            nodes.set_function("EndStaticAttribute", [] { ImNodes::EndStaticAttribute(); });
            nodes.set_function("Link", [](int id, int startAttr, int endAttr) {
                ImNodes::Link(id, startAttr, endAttr);
            });
            nodes.set_function("SetNodeGridSpacePos",
                               [](int id, float x, float y) { ImNodes::SetNodeGridSpacePos(id, ImVec2(x, y)); });
            nodes.set_function("SetNodeScreenSpacePos",
                               [](int id, float x, float y) { ImNodes::SetNodeScreenSpacePos(id, ImVec2(x, y)); });
            // IsLinkCreated() -> created, startAttr, endAttr
            nodes.set_function("IsLinkCreated", [] {
                int        s = 0, e = 0;
                const bool created = ImNodes::IsLinkCreated(&s, &e, nullptr);
                return std::make_tuple(created, s, e);
            });
            // IsLinkDestroyed() -> destroyed, linkId
            nodes.set_function("IsLinkDestroyed", [] {
                int        id        = 0;
                const bool destroyed = ImNodes::IsLinkDestroyed(&id);
                return std::make_tuple(destroyed, id);
            });
            // IsNodeHovered() -> hovered, nodeId
            nodes.set_function("IsNodeHovered", [] {
                int        id      = 0;
                const bool hovered = ImNodes::IsNodeHovered(&id);
                return std::make_tuple(hovered, id);
            });

            auto pin            = lua.create_table();
            pin["Circle"]       = static_cast<int>(ImNodesPinShape_Circle);
            pin["CircleFilled"] = static_cast<int>(ImNodesPinShape_CircleFilled);
            pin["Triangle"]     = static_cast<int>(ImNodesPinShape_Triangle);
            pin["Quad"]         = static_cast<int>(ImNodesPinShape_Quad);
            nodes["PinShape"]   = pin;
        }
    }
} // namespace vultra
