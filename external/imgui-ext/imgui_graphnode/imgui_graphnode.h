#pragma once

#ifndef IMGUI_GRAPHNODE_H_
#define IMGUI_GRAPHNODE_H_

#include "imgui.h"

#ifndef IMGUI_GRAPHNODE_NAMESPACE
#define IMGUI_GRAPHNODE_NAMESPACE ImGuiGraphNode
#endif /* !IMGUI_GRAPHNODE_NAMESPACE */

typedef int ImGuiGraphNodeLayout;

enum ImGuiGraphNodeLayout_
{
    ImGuiGraphNodeLayout_Circo,
    ImGuiGraphNodeLayout_Dot,
    ImGuiGraphNodeLayout_Fdp,
    ImGuiGraphNodeLayout_Neato,
    ImGuiGraphNodeLayout_Osage,
    ImGuiGraphNodeLayout_Sfdp,
    ImGuiGraphNodeLayout_Twopi
};

struct ImGuiGraphNodeRuntimeNodeStyle
{
    const char* id {nullptr};
    const char* title {nullptr};
    const char* tooltip {nullptr};
    const char* inputLabel {nullptr};
    const char* outputLabel {nullptr};
    const char* metadata {nullptr};
    ImU32       titleColor {0};
    ImU32       bodyColor {0};
    ImU32       borderColor {0};
    ImU32       textColor {0};
    ImU32       mutedTextColor {0};
    ImU32       pinColor {0};
    ImTextureID textureId {};
    bool        hasTexture {false};
};

namespace IMGUI_GRAPHNODE_NAMESPACE
{
    IMGUI_API void CreateContext();
    IMGUI_API void DestroyContext();
    IMGUI_API bool BeginNodeGraph(char const * id, ImGuiGraphNodeLayout layout = ImGuiGraphNodeLayout_Dot, float pixel_per_unit = 100.f);
    IMGUI_API bool NodeGraphLoadDot(char const* dot);
    IMGUI_API void NodeGraphSetView(ImVec2 viewport, float scale, bool auto_fit, bool center);
    IMGUI_API void NodeGraphSetGraphAttribute(char const* name, char const* value);
    IMGUI_API void NodeGraphAddNode(char const * id);
    IMGUI_API void NodeGraphAddNode(char const * id, ImVec4 const & color, ImVec4 const & fillcolor);
    IMGUI_API void NodeGraphAddNodeSized(char const* id, ImVec2 size, ImVec4 const& color, ImVec4 const& fillcolor);
    IMGUI_API void NodeGraphSetRuntimeNodeStyle(const ImGuiGraphNodeRuntimeNodeStyle& style);
    IMGUI_API void NodeGraphAddEdge(char const * id, char const * node_id_a, char const * node_id_b);
    IMGUI_API void NodeGraphAddEdge(char const * id, char const * node_id_a, char const * node_id_b, ImVec4 const & color);
    IMGUI_API void EndNodeGraph();
    IMGUI_API bool GetNodeGraphNodeRect(char const* id, ImVec2* min, ImVec2* max);
    IMGUI_API bool WasRuntimeNodeTextureDoubleClicked(char const* id);
}

#endif /* !IMGUI_GRAPHNODE_H_ */
