#include "graph_editor.hpp"

#include <imgui_node_editor.h>

#include <algorithm>
#include <array>
#include <format>
#include <set>

namespace vultra
{
    namespace nodes = ax::NodeEditor;

    namespace
    {
        constexpr ImVec4 kTextureColor {0.30f, 0.68f, 0.87f, 1};
        constexpr ImVec4 kBufferColor {0.79f, 0.61f, 0.31f, 1};
        constexpr ImVec4 kErrorColor {0.90f, 0.30f, 0.30f, 1};

        bool belongsTo(std::string_view port, std::string_view instance)
        {
            return port.starts_with(std::string(instance) + ".");
        }

        std::string_view portLabel(std::string_view port)
        {
            return port.substr(port.find('.') + 1);
        }

        void nodeSeparator()
        {
            const auto start = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(start, {start.x + 240, start.y}, IM_COL32(76, 78, 81, 255));
            ImGui::Dummy({240, 6}); // A regular Separator/Table would stretch to the canvas window width.
        }

        struct CurrentEditor
        {
            explicit CurrentEditor(nodes::EditorContext* context) :
                previous(nodes::GetCurrentEditor())
            {
                nodes::SetCurrentEditor(context);
            }

            ~CurrentEditor()
            {
                nodes::SetCurrentEditor(previous);
            }

            nodes::EditorContext* previous;
        };
    } // namespace

    struct GraphEditor::Impl
    {
        struct Pin
        {
            nodes::PinId     id;
            std::string      path;
            PassResourceKind kind;
            VriFormat        format;
            bool             input;
            bool             display;
            ImVec2           center;
        };

        explicit Impl(const PassCatalog& catalog) :
            catalog(catalog)
        {
            nodes::Config config;
            config.SettingsFile        = nullptr; // Layout belongs to .vworkspace, never a shared NodeEditor.json.
            config.NavigateButtonIndex = 2;
            context                    = nodes::CreateEditor(&config);
            CurrentEditor current(context);
            auto&         style                    = nodes::GetStyle();
            style.NodeRounding                     = 6;
            style.Colors[nodes::StyleColor_Bg]     = {0.075f, 0.08f, 0.09f, 1};
            style.Colors[nodes::StyleColor_Grid]   = {0.25f, 0.27f, 0.30f, 0.24f};
            style.Colors[nodes::StyleColor_NodeBg] = {0.12f, 0.13f, 0.14f, 1};
        }

        ~Impl()
        {
            nodes::DestroyEditor(context);
        }

        const PassCatalog&               catalog;
        nodes::EditorContext*            context;
        uintptr_t                        nextId = 1;
        std::map<std::string, uintptr_t> ids;
        std::vector<Pin>                 pins;
        std::map<uintptr_t, GraphEdge>   links;
        std::string                      contextPort;
        std::string                      contextNode;
        std::string                      rename;
        ImVec2                           addPosition {340, 40};
        bool                             fit      = true;
        size_t                           passType = 0;

        uintptr_t id(const std::string& key)
        {
            auto [found, inserted] = ids.try_emplace(key, nextId);
            if (inserted)
            {
                ++nextId;
            }
            return found->second;
        }

        void beginNode(ResearchDocument& document, const std::string& key, ImVec2 defaultPosition)
        {
            const bool fresh  = !ids.contains("node:" + key);
            const auto nodeId = nodes::NodeId(id("node:" + key));
            if (fresh)
            {
                const auto [position, inserted] =
                    document.nodePositions.try_emplace(key, defaultPosition.x, defaultPosition.y);
                nodes::SetNodePosition(nodeId, {position->second.x, position->second.y});
            }
            nodes::BeginNode(nodeId);
            ImGui::PushID(key.c_str());
            ImGui::BeginGroup();
            ImGui::Dummy({240, 0});
        }

        void endNode()
        {
            ImGui::EndGroup();
            ImGui::PopID();
            nodes::EndNode();
        }

        void
        pin(std::string path, const PassPort& port, bool input, const GraphDefinition& definition, bool display = false)
        {
            const auto pinId     = nodes::PinId(id(std::string(input ? "input:" : "output:") + path));
            auto       color     = port.kind == PassResourceKind::eTexture ? kTextureColor : kBufferColor;
            const bool connected = std::ranges::any_of(definition.edges,
                                                       [&](const auto& edge)
                                                       {
                                                           return edge.to == path;
                                                       });
            if (input && !connected && (!display || definition.outputs.empty()))
            {
                color = kErrorColor;
            }
            nodes::BeginPin(pinId, input ? nodes::PinKind::Input : nodes::PinKind::Output);
            nodes::PinPivotAlignment({input ? 0.0f : 1.0f, 0.5f});
            if (!input)
            {
                ImGui::TextUnformatted(portLabel(path).data());
                ImGui::SameLine(224);
            }
            ImGui::Dummy({16, 20});
            const auto   min = ImGui::GetItemRectMin();
            const auto   max = ImGui::GetItemRectMax();
            const ImVec2 center {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
            const bool   marked = std::ranges::find(definition.outputs, path) != definition.outputs.end();
            auto*        draw   = ImGui::GetWindowDrawList();
            draw->AddCircleFilled(center, 5, ImGui::ColorConvertFloat4ToU32(color));
            if (marked && !input)
            {
                draw->AddCircle(center, 8, IM_COL32(230, 206, 123, 255), 0, 2);
            }
            nodes::PinRect(min, max);
            nodes::PinPivotRect({center.x, center.y}, {center.x, center.y});
            if (input)
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(port.name.c_str());
            }
            nodes::EndPin();
            pins.push_back({pinId, std::move(path), port.kind, port.format, input, display, center});
        }

        const Pin* findPin(nodes::PinId pinId) const
        {
            const auto found = std::ranges::find(pins, pinId, &Pin::id);
            return found == pins.end() ? nullptr : &*found;
        }

        const Pin* findPin(std::string_view path, bool input) const
        {
            const auto found = std::ranges::find_if(pins,
                                                    [&](const Pin& value)
                                                    {
                                                        return value.path == path && value.input == input;
                                                    });
            return found == pins.end() ? nullptr : &*found;
        }

        void sceneNode(ResearchDocument& document)
        {
            beginNode(document, "scene", {20, 40});
            ImGui::TextColored(kTextureColor, "Scene renderer");
            const char* path = "Deferred";
            if (document.settings.path == RenderPath::eNaiveForward)
            {
                path = "Forward";
            }
            else if (document.settings.path == RenderPath::eReferencePathTracing)
            {
                path = "Reference path tracing";
            }
            ImGui::TextDisabled("%s", path);
            nodeSeparator();
            pin("scene.hdr", {"hdr", PassResourceKind::eTexture, VriFormat_RGBA16_SFLOAT}, false, document.definition);
            const auto depthFormat = document.settings.path == RenderPath::eReferencePathTracing ?
                                         VriFormat_RGBA32_SFLOAT :
                                         VriFormat_D32_SFLOAT;
            pin("scene.depth", {"depth", PassResourceKind::eTexture, depthFormat}, false, document.definition);
            if (document.settings.path == RenderPath::eReferencePathTracing)
            {
                constexpr std::array names {"radiance", "albedo", "normal", "motion", "sample_count", "ray_count"};
                for (const auto* name : names)
                {
                    pin(std::string("scene.") + name,
                        {name, PassResourceKind::eTexture, VriFormat_RGBA32_SFLOAT},
                        false,
                        document.definition);
                }
            }
            if (document.settings.path == RenderPath::eNaiveDeferred)
            {
                constexpr std::array names {"position_metallic",
                                            "normal_roughness",
                                            "albedo_weight",
                                            "emission_occlusion",
                                            "specular",
                                            "geometric_normal_ior",
                                            "coat"};
                for (const auto* name : names)
                {
                    pin(std::string("scene.") + name,
                        {name,
                         PassResourceKind::eTexture,
                         std::string_view(name) == "position_metallic" ? VriFormat_RGBA32_SFLOAT :
                                                                         VriFormat_RGBA16_SFLOAT},
                        false,
                        document.definition);
                }
            }
            endNode();
        }

        void displayNode(ResearchDocument& document)
        {
            beginNode(document, "display", {float(document.definition.passes.size() + 1) * 320, 40});
            ImGui::TextColored({0.78f, 0.71f, 0.46f, 1}, "Display output");
            ImGui::TextDisabled("HDR tone map / RGBA8 direct");
            nodeSeparator();
            pin("display.hdr",
                {"color", PassResourceKind::eTexture, VriFormat_Unknown},
                true,
                document.definition,
                true);
            ImGui::TextDisabled("display.final (RGBA8)");
            endNode();
        }

        void passNodes(EditorGuiFrame& ui, ResearchDocument& document, GraphEdit& edit)
        {
            for (size_t i = 0; i < document.definition.passes.size(); ++i)
            {
                auto&       pass       = document.definition.passes[i];
                const auto& definition = catalog.definition(pass.type);
                beginNode(document, "pass:" + pass.id, {float(i + 1) * 320, 40});
                ImGui::TextUnformatted(pass.id.c_str());
                ImGui::TextDisabled("%s", pass.type.c_str());
                nodeSeparator();
                for (const auto& port : definition.inputs)
                {
                    pin(pass.id + "." + port.name, port, true, document.definition);
                }
                ImGui::PushItemWidth(240);
                bool parametersChanged = false;
                for (const auto& parameter : definition.parameters)
                {
                    auto [value, inserted] = pass.parameters.try_emplace(parameter.name, parameter.defaultValue);
                    ImGui::PushID(parameter.name.c_str());
                    ui.textUnformatted(parameter.name.c_str());
                    ImGui::SameLine(80);
                    ImGui::SetNextItemWidth(160);
                    parametersChanged |= ImGui::SliderScalar("##value",
                                                             ImGuiDataType_Double,
                                                             &value->second,
                                                             &parameter.minimum,
                                                             &parameter.maximum,
                                                             "%.3f");
                    ImGui::PopID();
                }
                if (parametersChanged)
                {
                    edit.parameterEdits.push_back(pass.id);
                }
                ImGui::PopItemWidth();
                for (const auto& port : definition.outputs)
                {
                    pin(pass.id + "." + port.name, port, false, document.definition);
                }
                endNode();
            }
        }

        void drawLinks(const GraphDefinition& definition, std::string& status)
        {
            links.clear();
            for (const auto& edge : definition.edges)
            {
                const auto* from = findPin(edge.from, false);
                const auto* to   = findPin(edge.to, true);
                if (!from || !to || to->display)
                {
                    status = "Unresolved connection: " + edge.from + " -> " + edge.to;
                    continue;
                }
                const auto linkId = id("link:" + edge.to);
                links.emplace(linkId, edge);
                nodes::Link(nodes::LinkId(linkId),
                            from->id,
                            to->id,
                            from->kind == PassResourceKind::eTexture ? kTextureColor : kBufferColor,
                            2);
            }
            if (!definition.outputs.empty())
            {
                if (const auto* source = findPin(definition.outputs.front(), false))
                {
                    nodes::Link(nodes::LinkId(id("display-link")),
                                source->id,
                                findPin("display.hdr", true)->id,
                                {0.78f, 0.71f, 0.46f, 1},
                                2);
                }
            }
        }

        void createLink(GraphDefinition& definition, GraphEdit& edit, std::string& status)
        {
            if (nodes::BeginCreate(kTextureColor, 2))
            {
                nodes::PinId first;
                nodes::PinId second;
                if (nodes::QueryNewLink(&first, &second))
                {
                    const auto* from = findPin(first);
                    const auto* to   = findPin(second);
                    if (from && from->input)
                    {
                        std::swap(from, to);
                    }
                    if (from && to)
                    {
                        if (from->input || !to->input || from->kind != to->kind ||
                            (from->format != VriFormat_Unknown && to->format != VriFormat_Unknown &&
                             from->format != to->format) ||
                            (to->display && from->format != VriFormat_RGBA16_SFLOAT &&
                             from->format != VriFormat_RGBA8_UNORM))
                        {
                            status = "Connect an output to a matching input (texture/buffer and format)";
                            nodes::RejectNewItem(kErrorColor, 2);
                        }
                        else if (nodes::AcceptNewItem(kTextureColor, 2))
                        {
                            if (to->display)
                            {
                                std::erase(definition.outputs, from->path);
                                definition.outputs.insert(definition.outputs.begin(), from->path);
                                edit.preview = "display.final";
                            }
                            else
                            {
                                // A new wire replaces the input's old wire. The graph compiler still checks
                                // cycles/extents.
                                std::erase_if(definition.edges,
                                              [&](const auto& edge)
                                              {
                                                  return edge.to == to->path;
                                              });
                                definition.edges.push_back({from->path, to->path});
                            }
                            edit.changed = true;
                            status       = "Compiling connected graph at the next frame boundary";
                        }
                    }
                }
            }
            nodes::EndCreate();
        }

        void removePass(ResearchDocument& document, const std::string& instance)
        {
            auto& definition = document.definition;
            std::erase_if(definition.edges,
                          [&](const auto& edge)
                          {
                              return belongsTo(edge.from, instance) || belongsTo(edge.to, instance);
                          });
            std::erase_if(definition.outputs,
                          [&](const auto& output)
                          {
                              return belongsTo(output, instance);
                          });
            std::erase_if(definition.passes,
                          [&](const auto& pass)
                          {
                              return pass.id == instance;
                          });
            document.nodePositions.erase("pass:" + instance);
            ids.erase("node:pass:" + instance);
            std::erase_if(ids,
                          [&](const auto& entry)
                          {
                              return entry.first.starts_with("input:" + instance + ".") ||
                                     entry.first.starts_with("output:" + instance + ".");
                          });
        }

        void deleteItems(ResearchDocument& document, GraphEdit& edit)
        {
            auto& definition = document.definition;
            if (nodes::BeginDelete())
            {
                nodes::LinkId link;
                while (nodes::QueryDeletedLink(&link))
                {
                    const auto found = links.find(link.Get());
                    if (found != links.end() && nodes::AcceptDeletedItem())
                    {
                        std::erase_if(definition.edges,
                                      [&](const auto& edge)
                                      {
                                          return edge.to == found->second.to;
                                      });
                        edit.changed = true;
                    }
                    else
                    {
                        nodes::RejectDeletedItem();
                    }
                }
                nodes::NodeId node;
                while (nodes::QueryDeletedNode(&node))
                {
                    const auto found = std::ranges::find_if(definition.passes,
                                                            [&](const auto& pass)
                                                            {
                                                                return id("node:pass:" + pass.id) == node.Get();
                                                            });
                    if (found != definition.passes.end() && nodes::AcceptDeletedItem())
                    {
                        const auto instance = found->id;
                        removePass(document, instance);
                        edit.changed = true;
                    }
                    else
                    {
                        nodes::RejectDeletedItem();
                    }
                }
            }
            nodes::EndDelete();
        }

        void addPass(ResearchDocument& document, const PassDefinition& definition)
        {
            size_t      index = document.definition.passes.size();
            std::string instance;
            do
            {
                instance = std::format("pass_{}", index++);
            } while (std::ranges::any_of(document.definition.passes,
                                         [&](const auto& pass)
                                         {
                                             return pass.id == instance;
                                         }));
            document.definition.passes.push_back({instance, definition.type, {}});
            document.nodePositions.emplace("pass:" + instance, glm::vec2(addPosition.x, addPosition.y));
        }

        void menus(EditorGuiFrame& ui, ResearchDocument& document, GraphEdit& edit, std::string& status)
        {
            nodes::PinId  pinId;
            nodes::NodeId nodeId;
            nodes::LinkId linkId;
            if (nodes::ShowPinContextMenu(&pinId))
            {
                const auto* port = findPin(pinId);
                if (port && !port->input)
                {
                    contextPort = port->path;
                    ImGui::OpenPopup("Output port");
                }
            }
            if (nodes::ShowNodeContextMenu(&nodeId))
            {
                const auto found = std::ranges::find_if(document.definition.passes,
                                                        [&](const auto& pass)
                                                        {
                                                            return id("node:pass:" + pass.id) == nodeId.Get();
                                                        });
                if (found != document.definition.passes.end())
                {
                    contextNode = found->id;
                    rename      = contextNode;
                    ImGui::OpenPopup("Pass instance");
                }
            }
            if (nodes::ShowLinkContextMenu(&linkId))
            {
                if (const auto found = links.find(linkId.Get()); found != links.end())
                {
                    std::erase_if(document.definition.edges,
                                  [&](const auto& edge)
                                  {
                                      return edge.to == found->second.to;
                                  });
                    edit.changed = true;
                }
            }
            if (nodes::ShowBackgroundContextMenu())
            {
                addPosition = nodes::ScreenToCanvas(ImGui::GetMousePos());
                ImGui::OpenPopup("Add pass");
            }
            nodes::Suspend();
            if (ImGui::BeginPopup("Add pass"))
            {
                for (const auto& definition : catalog.definitions())
                {
                    if (ImGui::MenuItem(definition.type.c_str()))
                    {
                        addPass(document, definition);
                        edit.changed = true;
                    }
                }
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopup("Pass instance"))
            {
                ui.inputText("Instance ID", &rename);
                if (ui.button("Rename"))
                {
                    const bool duplicate = std::ranges::any_of(document.definition.passes,
                                                               [&](const auto& pass)
                                                               {
                                                                   return pass.id == rename && pass.id != contextNode;
                                                               });
                    if (rename.empty() || rename.find('.') != std::string::npos || rename == "scene" || duplicate)
                    {
                        status = "Instance ID must be unique, nonempty, without '.' and different from scene";
                    }
                    else
                    {
                        const auto found =
                            std::ranges::find(document.definition.passes, contextNode, &GraphPassDesc::id);
                        if (found != document.definition.passes.end())
                        {
                            const auto replace = [&](std::string& endpoint)
                            {
                                if (belongsTo(endpoint, contextNode))
                                {
                                    endpoint = rename + endpoint.substr(contextNode.size());
                                }
                            };
                            for (auto& edge : document.definition.edges)
                            {
                                replace(edge.from);
                                replace(edge.to);
                            }
                            for (auto& output : document.definition.outputs)
                            {
                                replace(output);
                            }
                            const auto oldKey   = "pass:" + contextNode;
                            const auto newKey   = "pass:" + rename;
                            const auto position = nodes::GetNodePosition(nodes::NodeId(id("node:" + oldKey)));
                            document.nodePositions.erase(oldKey);
                            document.nodePositions[newKey] = {position.x, position.y};
                            found->id                      = rename;
                            edit.changed                   = true;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ui.sameLine();
                if (ui.button("Delete pass"))
                {
                    nodes::DeleteNode(nodes::NodeId(id("node:pass:" + contextNode)));
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopup("Output port"))
            {
                ImGui::TextUnformatted(contextPort.c_str());
                ImGui::Separator();
                auto&      outputs = document.definition.outputs;
                const bool marked  = std::ranges::find(outputs, contextPort) != outputs.end();
                if (ImGui::MenuItem("Preview", nullptr, false, marked))
                {
                    edit.preview = contextPort;
                }
                if (ImGui::MenuItem("Mark for preview / export", nullptr, marked))
                {
                    if (marked)
                    {
                        std::erase(outputs, contextPort);
                    }
                    else
                    {
                        outputs.push_back(contextPort);
                        edit.preview = contextPort;
                    }
                    edit.changed = true;
                }
                const auto* port = findPin(contextPort, false);
                if (ImGui::MenuItem(
                        "Use as display input",
                        nullptr,
                        false,
                        port && port->kind == PassResourceKind::eTexture &&
                            (port->format == VriFormat_RGBA16_SFLOAT || port->format == VriFormat_RGBA8_UNORM)))
                {
                    std::erase(outputs, contextPort);
                    outputs.insert(outputs.begin(), contextPort);
                    edit.changed = true;
                    edit.preview = "display.final";
                }
                ImGui::EndPopup();
            }
            nodes::Resume();
        }

        void saveLayout(ResearchDocument& document) const
        {
            std::set<std::string> keys {"scene", "display"};
            for (const auto& pass : document.definition.passes)
            {
                keys.insert("pass:" + pass.id);
            }
            for (const auto& key : keys)
            {
                if (const auto found = ids.find("node:" + key); found != ids.end())
                {
                    const auto position         = nodes::GetNodePosition(nodes::NodeId(found->second));
                    document.nodePositions[key] = {position.x, position.y};
                }
            }
            std::erase_if(document.nodePositions,
                          [&](const auto& entry)
                          {
                              return !keys.contains(entry.first);
                          });
        }
    };

    GraphEditor::GraphEditor(const PassCatalog& catalog) :
        m_Impl(std::make_unique<Impl>(catalog))
    {
    }

    GraphEditor::~GraphEditor() = default;

    void GraphEditor::reset()
    {
        const auto& catalog = m_Impl->catalog;
        m_Impl              = std::make_unique<Impl>(catalog);
    }

    void GraphEditor::saveLayout(ResearchDocument& document) const
    {
        CurrentEditor current(m_Impl->context);
        m_Impl->saveLayout(document);
    }

    std::optional<ImVec2> GraphEditor::portPosition(std::string_view path, bool input) const
    {
        if (const auto* pin = m_Impl->findPin(path, input))
        {
            return pin->center;
        }
        return std::nullopt;
    }

    GraphEdit GraphEditor::draw(EditorGuiFrame& ui, ResearchDocument& document, std::string& status)
    {
        auto&         state = *m_Impl;
        CurrentEditor current(state.context);
        GraphEdit     edit;
        const auto    definitions = state.catalog.definitions();
        ImGui::SetNextItemWidth(230);
        if (ImGui::BeginCombo("##pass-type", definitions[state.passType].type.c_str()))
        {
            for (size_t i = 0; i < definitions.size(); ++i)
            {
                if (ui.selectable(definitions[i].type.c_str(), i == state.passType))
                {
                    state.passType = i;
                }
            }
            ImGui::EndCombo();
        }
        ui.sameLine();
        if (ui.button("Add pass"))
        {
            state.addPosition = {340, float(document.definition.passes.size()) * 220 + 40};
            state.addPass(document, definitions[state.passType]);
            edit.changed = true;
        }
        ui.sameLine();
        const bool autoLayout = ui.button("Arrange");
        ui.sameLine();
        state.fit |= ui.button("Fit");
        ui.textDisabled("Drag pins to connect | MMB pan | Wheel zoom | Delete selection | RMB output menu");
        nodes::Begin("Graph definition canvas");
        state.pins.clear();
        state.sceneNode(document);
        state.passNodes(ui, document, edit);
        state.displayNode(document);
        state.drawLinks(document.definition, status);
        state.createLink(document.definition, edit, status);
        state.deleteItems(document, edit);
        state.menus(ui, document, edit, status);
        if (autoLayout)
        {
            nodes::SetNodePosition(nodes::NodeId(state.id("node:scene")), {20, 40});
            for (size_t i = 0; i < document.definition.passes.size(); ++i)
            {
                nodes::SetNodePosition(nodes::NodeId(state.id("node:pass:" + document.definition.passes[i].id)),
                                       {float(i + 1) * 320, 40});
            }
            nodes::SetNodePosition(nodes::NodeId(state.id("node:display")),
                                   {float(document.definition.passes.size() + 1) * 320, 40});
            state.fit = true;
        }
        nodes::End();
        if (state.fit)
        {
            nodes::NavigateToContent(0);
            state.fit = false;
        }
        if (const auto* pin = state.findPin(nodes::GetDoubleClickedPin()); pin && !pin->input)
        {
            if (std::ranges::find(document.definition.outputs, pin->path) == document.definition.outputs.end())
            {
                document.definition.outputs.push_back(pin->path);
                edit.changed = true;
            }
            edit.preview = pin->path;
        }
        for (auto& pin : state.pins)
        {
            pin.center = nodes::CanvasToScreen(pin.center);
        }
        state.saveLayout(document);
        return edit;
    }
} // namespace vultra
