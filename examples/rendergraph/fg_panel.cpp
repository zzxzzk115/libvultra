#include "fg_panel.hpp"
#include "pass_registry.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

using json = nlohmann::json;

static ImColor GetPinColor(ResourceType type)
{
    switch (type)
    {
        case ResourceType::eTexture:
            return ImColor(100, 150, 255);
        case ResourceType::eBuffer:
            return ImColor(255, 200, 80);
        default:
            return ImColor(200, 200, 200);
    }
}

void FrameGraphEditorPanel::initialize()
{
    ed::Config cfg;
    cfg.SettingsFile = nullptr;
    m_NodeEditorCtx  = ed::CreateEditor(&cfg);

    ed::SetCurrentEditor(m_NodeEditorCtx);
    loadGraphFromFile("graph.vfg");
}

void FrameGraphEditorPanel::shutdown()
{
    saveGraphToFile("graph.vfg");

    if (m_NodeEditorCtx)
    {
        ed::DestroyEditor(m_NodeEditorCtx);
        m_NodeEditorCtx = nullptr;
    }
}

void FrameGraphEditorPanel::draw()
{
    ed::SetCurrentEditor(m_NodeEditorCtx);
    ed::Begin("FrameGraphEditor");

    // Draw nodes
    for (auto& node : m_Nodes)
    {
        ed::BeginNode(node.id);
        ImGui::Text("%s", node.displayName.c_str());

        node.position = ed::GetNodePosition(node.id);

        for (auto& pin : node.pins)
        {
            ed::BeginPin(pin.id, pin.kind);
            ImGui::PushID(pin.id.Get());

            if (pin.kind == ed::PinKind::Input)
                ImGui::TextColored(GetPinColor(pin.type), "-> %s", pin.name.c_str());
            else
                ImGui::TextColored(GetPinColor(pin.type), "%s ->", pin.name.c_str());

            ImGui::PopID();
            ed::EndPin();
        }
        ed::EndNode();
    }

    // Draw links
    for (auto& link : m_Links)
        ed::Link(link.id, link.from, link.to);

    // Create link
    if (ed::BeginCreate())
    {
        ed::PinId a, b;
        if (ed::QueryNewLink(&a, &b))
        {
            FPin* pa = findPinById(a);
            FPin* pb = findPinById(b);
            if (!pa || !pb)
            {
                ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
            }
            else if (pa->kind == pb->kind)
            {
                ed::RejectNewItem(ImColor(255, 64, 64), 2.0f);
            }
            else if (pa->type != pb->type)
            {
                ed::RejectNewItem(ImColor(255, 128, 0), 2.0f);
            }
            else
            {
                if (ed::AcceptNewItem())
                {
                    FLink link;
                    link.id   = m_NextLinkId++;
                    link.from = (pa->kind == ed::PinKind::Output) ? pa->id : pb->id;
                    link.to   = (pa->kind == ed::PinKind::Input) ? pa->id : pb->id;
                    m_Links.push_back(link);
                }
            }
        }
    }
    ed::EndCreate();

    // Delete
    if (ed::BeginDelete())
    {
        ed::LinkId lid;
        while (ed::QueryDeletedLink(&lid))
            if (ed::AcceptDeletedItem())
                deleteLink(lid);

        ed::NodeId nid;
        while (ed::QueryDeletedNode(&nid))
            if (ed::AcceptDeletedItem())
                deleteNode(nid);
    }
    ed::EndDelete();

    drawContextMenus();

    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S))
        saveGraphToFile("graph.vfg");

    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_O))
        loadGraphFromFile("graph.vfg");

    ed::End();
}

void FrameGraphEditorPanel::drawContextMenus()
{
    ed::Suspend();

    // Right click on background
    if (ed::ShowBackgroundContextMenu())
        ImGui::OpenPopup("CreateNodeMenu");

    if (ImGui::BeginPopup("CreateNodeMenu"))
    {
        for (auto& [name, entry] : PassRegistry::instance().getEntries())
        {
            if (ImGui::MenuItem(name.c_str()))
            {
                ImVec2 pos = ed::ScreenToCanvas(ImGui::GetMousePosOnOpeningCurrentPopup());
                createPassNode(name, pos);
            }
        }
        ImGui::EndPopup();
    }

    // Right click on node
    if (ed::ShowNodeContextMenu(&m_ContextNodeId))
        ImGui::OpenPopup("NodeContextMenu");

    if (ImGui::BeginPopup("NodeContextMenu"))
    {
        if (ImGui::MenuItem("Delete Pass"))
            deleteNode(m_ContextNodeId);
        ImGui::EndPopup();
    }

    // Right click on link
    if (ed::ShowLinkContextMenu(&m_ContextLinkId))
        ImGui::OpenPopup("LinkContextMenu");

    if (ImGui::BeginPopup("LinkContextMenu"))
    {
        if (ImGui::MenuItem("Delete Link"))
            deleteLink(m_ContextLinkId);
        ImGui::EndPopup();
    }

    ed::Resume();
}

void FrameGraphEditorPanel::createPassNode(const std::string& passType, ImVec2 pos)
{
    FNode n;
    n.id           = m_NextNodeId++;
    n.passType     = passType;
    n.instanceName = passType + "_" + std::to_string(n.id.Get());
    n.displayName  = passType;
    n.position     = pos;

    auto it = PassRegistry::instance().getEntries().find(passType);
    if (it != PassRegistry::instance().getEntries().end())
    {
        auto pass    = it->second.factory();
        auto reflect = pass->reflect();

        auto typeFromString = [](const std::string& s) -> ResourceType {
            if (s == "Texture")
                return ResourceType::eTexture;
            if (s == "Buffer")
                return ResourceType::eBuffer;
            return ResourceType::eUnknown;
        };

        for (auto& in : reflect.inputs)
            n.pins.push_back({ed::PinId(m_NextPinId++), in.name, typeFromString(in.type), ed::PinKind::Input});

        for (auto& out : reflect.outputs)
            n.pins.push_back({ed::PinId(m_NextPinId++), out.name, typeFromString(out.type), ed::PinKind::Output});
    }

    m_Nodes.push_back(n);
    ed::SetNodePosition(n.id, n.position);
}

FPin* FrameGraphEditorPanel::findPinById(ed::PinId id)
{
    for (auto& n : m_Nodes)
        for (auto& p : n.pins)
            if (p.id == id)
                return &p;
    return nullptr;
}

void FrameGraphEditorPanel::deleteNode(ed::NodeId id)
{
    m_Nodes.erase(std::remove_if(m_Nodes.begin(), m_Nodes.end(), [&](auto& n) { return n.id == id; }), m_Nodes.end());
}

void FrameGraphEditorPanel::deleteLink(ed::LinkId id)
{
    m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(), [&](auto& l) { return l.id == id; }), m_Links.end());
}

void FrameGraphEditorPanel::saveGraphToFile(const std::string& path)
{
    json root;
    json jNodes = json::array();
    json jLinks = json::array();

    for (auto& n : m_Nodes)
    {
        json jNode;
        jNode["id"]       = n.id.Get();
        jNode["type"]     = n.passType;
        jNode["name"]     = n.displayName;
        jNode["position"] = {n.position.x, n.position.y};

        json jInputs  = json::array();
        json jOutputs = json::array();

        for (auto& p : n.pins)
        {
            if (p.kind == ed::PinKind::Input)
                jInputs.push_back(p.name);
            else
                jOutputs.push_back(p.name);
        }

        jNode["inputs"]  = jInputs;
        jNode["outputs"] = jOutputs;
        jNodes.push_back(jNode);
    }

    for (auto& l : m_Links)
    {
        FPin* from = findPinById(l.from);
        FPin* to   = findPinById(l.to);
        if (!from || !to)
            continue;

        int fromNode = 0, toNode = 0;
        for (auto& n : m_Nodes)
        {
            for (auto& p : n.pins)
            {
                if (p.id == from->id)
                    fromNode = n.id.Get();
                if (p.id == to->id)
                    toNode = n.id.Get();
            }
        }

        json jLink;
        jLink["fromNode"] = fromNode;
        jLink["fromPin"]  = from->name;
        jLink["toNode"]   = toNode;
        jLink["toPin"]    = to->name;
        jLinks.push_back(jLink);
    }

    root["passes"] = jNodes;
    root["links"]  = jLinks;

    root["id_counters"] = {
        {"nextNodeId", m_NextNodeId},
        {"nextPinId", m_NextPinId},
        {"nextLinkId", m_NextLinkId},
    };

    std::ofstream ofs(path);
    ofs << root.dump(4);
    ofs.close();
}

void FrameGraphEditorPanel::loadGraphFromFile(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open())
        return;

    json root;
    ifs >> root;
    ifs.close();

    m_Nodes.clear();
    m_Links.clear();

    if (root.contains("id_counters"))
    {
        auto c       = root["id_counters"];
        m_NextNodeId = c.value("nextNodeId", m_NextNodeId);
        m_NextPinId  = c.value("nextPinId", m_NextPinId);
        m_NextLinkId = c.value("nextLinkId", m_NextLinkId);
    }

    for (auto& jn : root["passes"])
    {
        FNode n;
        n.id           = ed::NodeId(jn["id"].get<int>());
        n.passType     = jn["type"].get<std::string>();
        n.displayName  = jn["name"].get<std::string>();
        n.instanceName = n.passType + "_" + std::to_string(n.id.Get());
        n.position     = ImVec2(jn["position"][0], jn["position"][1]);

        auto typeFromString = [](const std::string& s) -> ResourceType {
            if (s == "Texture")
                return ResourceType::eTexture;
            if (s == "Buffer")
                return ResourceType::eBuffer;
            return ResourceType::eUnknown;
        };

        auto it = PassRegistry::instance().getEntries().find(n.passType);
        if (it != PassRegistry::instance().getEntries().end())
        {
            auto pass    = it->second.factory();
            auto reflect = pass->reflect();
            for (auto& in : reflect.inputs)
                n.pins.push_back({ed::PinId(m_NextPinId++), in.name, typeFromString(in.type), ed::PinKind::Input});
            for (auto& out : reflect.outputs)
                n.pins.push_back({ed::PinId(m_NextPinId++), out.name, typeFromString(out.type), ed::PinKind::Output});
        }

        m_Nodes.push_back(n);
        ed::SetNodePosition(n.id, n.position);
    }

    for (auto& jl : root["links"])
    {
        int         fromNodeId  = jl["fromNode"];
        int         toNodeId    = jl["toNode"];
        std::string fromPinName = jl["fromPin"];
        std::string toPinName   = jl["toPin"];

        ed::PinId fromPin, toPin;

        for (auto& n : m_Nodes)
        {
            if (n.id.Get() == fromNodeId)
                for (auto& p : n.pins)
                    if (p.name == fromPinName)
                        fromPin = p.id;

            if (n.id.Get() == toNodeId)
                for (auto& p : n.pins)
                    if (p.name == toPinName)
                        toPin = p.id;
        }

        FLink link;
        link.id   = ed::LinkId(m_NextLinkId++);
        link.from = fromPin;
        link.to   = toPin;
        m_Links.push_back(link);
    }
}