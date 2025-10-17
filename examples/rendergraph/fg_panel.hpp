#pragma once

#include <imgui.h>
#include <imgui_node_editor.h>
#include <string>
#include <vector>

namespace ed = ax::NodeEditor;

enum class ResourceType
{
    eUnknown,
    eTexture,
    eBuffer
};

struct FPin
{
    ed::PinId    id;
    std::string  name;
    ResourceType type;
    ed::PinKind  kind;
};

struct FNode
{
    ed::NodeId        id;
    std::string       passType;
    std::string       instanceName;
    std::string       displayName;
    ImVec2            position;
    std::vector<FPin> pins;
};

struct FLink
{
    ed::LinkId id;
    ed::PinId  from;
    ed::PinId  to;
};

class FrameGraphEditorPanel
{
public:
    void initialize();
    void draw();
    void shutdown();

private:
    void  drawContextMenus();
    void  createPassNode(const std::string& passType, ImVec2 pos);
    void  deleteNode(ed::NodeId id);
    void  deleteLink(ed::LinkId id);
    FPin* findPinById(ed::PinId id);

	void saveGraphToFile(const std::string& path);
	void loadGraphFromFile(const std::string& path);

    ed::EditorContext* m_NodeEditorCtx = nullptr;
    std::vector<FNode> m_Nodes;
    std::vector<FLink> m_Links;

    int m_NextNodeId = 1;
    int m_NextPinId  = 10000;
    int m_NextLinkId = 20000;

    ed::NodeId m_ContextNodeId = 0;
    ed::LinkId m_ContextLinkId = 0;
};
