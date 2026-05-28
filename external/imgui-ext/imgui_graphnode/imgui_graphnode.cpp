#include "imgui_graphnode.h"
#include "imgui_graphnode_internal.h"
#include "imgui_internal.h"

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <string>

extern "C" {
extern gvplugin_library_t vultra_gvplugin_core_library;
extern gvplugin_library_t vultra_gvplugin_dot_layout_library;
}

static float IsPointInRectangle_IsLeft(ImVec2 const & p0, ImVec2 const & p1, ImVec2 const & p2)
{
    return (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
}

static bool IsPointInRectangle(ImVec2 const & a, ImVec2 const & b, ImVec2 const & c, ImVec2 const & d, ImVec2 const & p)
{
    return IsPointInRectangle_IsLeft(a, b, p) > 0
        && IsPointInRectangle_IsLeft(b, c, p) > 0
        && IsPointInRectangle_IsLeft(c, d, p) > 0
        && IsPointInRectangle_IsLeft(d, a, p) > 0;
}

static ImGuiID GraphvizNodeNameToImGuiID(const std::string& name)
{
    if (!name.empty() &&
        std::all_of(name.begin(), name.end(), [](const char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        }))
    {
        return static_cast<ImGuiID>(std::strtoul(name.c_str(), nullptr, 10));
    }
    return ImGui::GetID(name.c_str());
}

static void ApplyRuntimeNodeStyleToGraph(ImGuiGraphNodeContextCache& cache,
                                         const ImGuiGraphNodeRuntimeNodeStyle& style)
{
    if (!g_ctx.gvgraph || !style.id)
        return;

    Agnode_t* node = agnode(g_ctx.gvgraph, const_cast<char*>(style.id), 0);
    if (!node)
        return;

    const char* title = style.title ? style.title : style.id;
    const float titleWidthPx = ImGui::CalcTextSize(title).x + 36.0f;
    const float charWidthPx = ImGui::GetFontSize() * 0.62f;
    const float nameWidthPx = static_cast<float>(std::strlen(title)) * charWidthPx + 36.0f;
    const float minWidthPx = style.hasTexture ? 360.0f : 220.0f;
    const float minHeightPx = style.hasTexture ? 276.0f : 36.0f;
    const float widthIn = ImClamp(ImMax(ImMax(titleWidthPx, nameWidthPx), minWidthPx) / cache.pixel_per_unit,
                                  1.2f,
                                  6.5f);
    const float heightIn = minHeightPx / cache.pixel_per_unit;

    char width[32];
    char height[32];
    snprintf(width, sizeof(width), "%.4f", widthIn);
    snprintf(height, sizeof(height), "%.4f", heightIn);
    agsafeset(node, (char*)"width", width, "");
    agsafeset(node, (char*)"height", height, "");
    agsafeset(node, (char*)"fixedsize", (char*)"true", "");
    agsafeset(node, (char*)"shape", (char*)"box", "");
}

static void AppendRuntimeNodeStyleGraphId(ImGuiGraphNodeContextCache& cache,
                                          const ImGuiGraphNodeRuntimeNodeStyle& style)
{
    if (!style.id)
        return;

    const char* title = style.title ? style.title : style.id;
    const float titleWidthPx = ImGui::CalcTextSize(title).x + 36.0f;
    const float charWidthPx = ImGui::GetFontSize() * 0.62f;
    const float nameWidthPx = static_cast<float>(std::strlen(title)) * charWidthPx + 36.0f;
    const float minWidthPx = style.hasTexture ? 360.0f : 220.0f;
    const float minHeightPx = style.hasTexture ? 276.0f : 36.0f;
    const float widthIn = ImClamp(ImMax(ImMax(titleWidthPx, nameWidthPx), minWidthPx) / cache.pixel_per_unit,
                                  1.2f,
                                  6.5f);
    const float heightIn = minHeightPx / cache.pixel_per_unit;

    char width[32];
    char height[32];
    snprintf(width, sizeof(width), "%.4f", widthIn);
    snprintf(height, sizeof(height), "%.4f", heightIn);
    cache.graphid_current += "runtime-style:";
    cache.graphid_current += style.id;
    cache.graphid_current += ":";
    cache.graphid_current += title;
    cache.graphid_current += ":";
    cache.graphid_current += width;
    cache.graphid_current += "x";
    cache.graphid_current += height;
}

static bool DrawRuntimeNodeBody(const ImGuiGraphNodeRuntimeNodeStyle& style, const ImRect& rect)
{
    if (!style.id)
        return false;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 nodeMin = rect.Min;
    const ImVec2 nodeMax = rect.Max;
    const ImU32 titleColor = style.titleColor ? style.titleColor : IM_COL32(70, 138, 148, 255);
    const ImU32 bodyColor = style.bodyColor ? style.bodyColor : IM_COL32(24, 42, 44, 255);
    const ImU32 borderColor = style.borderColor ? style.borderColor : IM_COL32(78, 96, 110, 220);
    const ImU32 textColor = style.textColor ? style.textColor : IM_COL32(235, 242, 248, 255);
    const ImU32 mutedTextColor = style.mutedTextColor ? style.mutedTextColor : IM_COL32(150, 166, 182, 255);
    const ImU32 pinColor = style.pinColor ? style.pinColor : IM_COL32(74, 148, 220, 255);
    const bool hasTexture = style.hasTexture && style.textureId;
    const float titleHeight = hasTexture ? 26.0f : (nodeMax.y - nodeMin.y);
    constexpr float rounding = 6.0f;

    if (hasTexture)
    {
        drawList->AddRectFilled(nodeMin, nodeMax, bodyColor, rounding);
        drawList->AddRectFilled(nodeMin,
                                ImVec2 {nodeMax.x, nodeMin.y + titleHeight},
                                titleColor,
                                rounding,
                                ImDrawFlags_RoundCornersTop);
    }
    else
    {
        drawList->AddRectFilled(nodeMin, nodeMax, titleColor, rounding);
    }
    drawList->AddRect(nodeMin, nodeMax, borderColor, rounding);

    const char* title = style.title ? style.title : style.id;
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    const ImVec2 titleMin {
        hasTexture ? (nodeMin.x + 8.0f) : (nodeMin.x + std::max(8.0f, (nodeMax.x - nodeMin.x - titleSize.x) * 0.5f)),
        nodeMin.y + std::max(2.0f, (titleHeight - titleSize.y) * 0.5f)
    };
    const ImVec2 titleMax {nodeMax.x - 8.0f, nodeMin.y + titleHeight};
    const ImVec4 titleClip {nodeMin.x + 8.0f, nodeMin.y, titleMax.x, titleMax.y};
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), titleMin, textColor, title, nullptr, 0.0f, &titleClip);

    (void)pinColor;

    ImGui::PushID(style.id);
    if (style.tooltip && style.tooltip[0] != '\0')
    {
        ImGui::SetCursorScreenPos(nodeMin);
        ImGui::InvisibleButton("GraphNodeTitle", ImVec2 {nodeMax.x - nodeMin.x, titleHeight});
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", style.tooltip);
    }

    bool doubleClicked = false;
    if (hasTexture)
    {
        const float thumbW = (nodeMax.x - nodeMin.x) - 20.0f;
        const float thumbH = thumbW * 9.0f / 16.0f;
        const ImVec2 imageMin {nodeMin.x + 10.0f, nodeMin.y + 42.0f};
        const ImVec2 imageMax {imageMin.x + thumbW, imageMin.y + thumbH};
        drawList->AddRectFilled(imageMin, imageMax, IM_COL32(0, 0, 0, 255), rounding);
        drawList->AddImage(style.textureId, imageMin, imageMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        drawList->AddRect(imageMin, imageMax, IM_COL32(70, 84, 98, 255), rounding);
        if (style.metadata && style.metadata[0] != '\0')
            drawList->AddText(ImVec2 {nodeMin.x + 10.0f, imageMax.y + 6.0f}, mutedTextColor, style.metadata);

        ImGui::SetCursorScreenPos(imageMin);
        ImGui::InvisibleButton("GraphNodeTexture", ImVec2 {thumbW, thumbH});
        doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Double-click to open full preview");
    }
    ImGui::PopID();
    return doubleClicked;
}

static void DrawRuntimeNodeFallback(const ImGuiGraphNode_DrawNode& node, const ImRect& rect)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    constexpr float rounding = 6.0f;
    constexpr float titleHeight = 24.0f;
    const ImU32 titleColor = node.fillcolor ? node.fillcolor : IM_COL32(70, 138, 148, 255);
    const ImU32 borderColor = node.color ? node.color : IM_COL32(78, 96, 110, 220);
    const ImU32 textColor = IM_COL32(218, 232, 240, 255);

    drawList->AddRectFilled(rect.Min, rect.Max, titleColor, rounding);
    drawList->AddRect(rect.Min, rect.Max, borderColor, rounding);

    const char* label = node.text ? node.text : "node";
    const ImVec2 titleSize = ImGui::CalcTextSize(label);
    const ImVec2 titleMin {
        rect.Min.x + std::max(8.0f, (rect.Max.x - rect.Min.x - titleSize.x) * 0.5f),
        rect.Min.y + std::max(2.0f, (rect.Max.y - rect.Min.y - titleSize.y) * 0.5f)
    };
    const ImVec4 titleClip {rect.Min.x + 8.0f, rect.Min.y, rect.Max.x - 8.0f, rect.Max.y};
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), titleMin, textColor, label, nullptr, 0.0f, &titleClip);
    (void)titleHeight;
}

static ImVec2 CubicBezierPoint(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t)
{
    const float u = 1.0f - t;
    const float uu = u * u;
    const float tt = t * t;
    const float uuu = uu * u;
    const float ttt = tt * t;
    return ImVec2 {
        uuu * p0.x + 3.0f * uu * t * p1.x + 3.0f * u * tt * p2.x + ttt * p3.x,
        uuu * p0.y + 3.0f * uu * t * p1.y + 3.0f * u * tt * p2.y + ttt * p3.y,
    };
}

static ImVec2 GraphvizSplinePoint(const std::vector<ImVec2>& points, float t)
{
    if (points.size() >= 4 && ((points.size() - 1) % 3) == 0)
    {
        const int segmentCount = static_cast<int>((points.size() - 1) / 3);
        const float segment = ImClamp(t, 0.0f, 0.999999f) * static_cast<float>(segmentCount);
        const int segmentIndex = ImClamp(static_cast<int>(segment), 0, segmentCount - 1);
        const float localT = segment - static_cast<float>(segmentIndex);
        const size_t base = static_cast<size_t>(segmentIndex * 3);
        return CubicBezierPoint(points[base + 0], points[base + 1], points[base + 2], points[base + 3], localT);
    }

    const float segment = (static_cast<float>(points.size() - 1) * ImClamp(t, 0.0f, 1.0f));
    const auto index = static_cast<size_t>(ImClamp(static_cast<int>(segment), 0, static_cast<int>(points.size() - 2)));
    const float localT = segment - static_cast<float>(index);
    return ImLerp(points[index], points[index + 1], localT);
}

void IMGUI_GRAPHNODE_NAMESPACE::CreateContext()
{
    if (g_ctx.gvcontext != nullptr)
        return;
    g_ctx.gvcontext = gvContext();
    gvAddLibrary(g_ctx.gvcontext, &vultra_gvplugin_core_library);
    gvAddLibrary(g_ctx.gvcontext, &vultra_gvplugin_dot_layout_library);
}

void IMGUI_GRAPHNODE_NAMESPACE::DestroyContext()
{
    if (g_ctx.gvgraph)
    {
        agclose(g_ctx.gvgraph);
        g_ctx.gvgraph = nullptr;
    }
    if (g_ctx.gvcontext == nullptr)
        return;
    gvFreeContext(g_ctx.gvcontext);
    g_ctx.gvcontext = nullptr;
    g_ctx.graph_caches.clear();
}

void IMGUI_GRAPHNODE_NAMESPACE::ClearNodeGraphCaches()
{
    g_ctx.graph_caches.clear();
    g_ctx.lastid = 0;
}

bool IMGUI_GRAPHNODE_NAMESPACE::BeginNodeGraph(char const * id, ImGuiGraphNodeLayout layout, float pixel_per_unit)
{
    g_ctx.lastid = ImGui::GetID(id);
    auto & cache = g_ctx.graph_caches[g_ctx.lastid];
    if (g_ctx.gvcontext == nullptr || g_ctx.gvgraph != nullptr)
        return false;
    cache.graphid_current.clear();
    cache.runtimeNodeStyles.clear();
    cache.runtimeTextureDoubleClicked.clear();
    cache.raw_dot_current.clear();
    cache.raw_dot_loaded = false;
    g_ctx.gvgraph = agopen(const_cast<char *>("g"), Agdirected, 0);
    if (g_ctx.gvgraph == nullptr)
        return false;
    cache.layout = layout;
    cache.pixel_per_unit = pixel_per_unit;

    char graphid_buf[16] = { 0 };
    snprintf(graphid_buf, sizeof(graphid_buf) - 1, "%d", (int)layout);
    cache.graphid_current += graphid_buf;

    return true;
}

bool IMGUI_GRAPHNODE_NAMESPACE::NodeGraphLoadDot(char const* dot)
{
    auto& cache = g_ctx.graph_caches[g_ctx.lastid];
    if (!dot || dot[0] == '\0' || g_ctx.gvgraph == nullptr)
        return false;

    cache.graphid_current += "raw-dot:";
    cache.graphid_current += dot;
    cache.raw_dot_current = dot;
    if (cache.raw_dot_current == cache.raw_dot_previous && cache.graph.size.x > 0.0f && cache.graph.size.y > 0.0f)
        return true;

    agclose(g_ctx.gvgraph);
    g_ctx.gvgraph = agmemread(dot);
    if (g_ctx.gvgraph == nullptr)
    {
        g_ctx.gvgraph = agopen(const_cast<char*>("g"), Agdirected, 0);
        return false;
    }
    cache.raw_dot_loaded = true;
    return true;
}

void IMGUI_GRAPHNODE_NAMESPACE::NodeGraphSetGraphAttribute(char const* name, char const* value)
{
    auto& cache = g_ctx.graph_caches[g_ctx.lastid];
    IM_ASSERT(g_ctx.gvgraph != nullptr);
    agsafeset(g_ctx.gvgraph, const_cast<char*>(name), const_cast<char*>(value), const_cast<char*>(""));
    cache.graphid_current += name;
    cache.graphid_current += value;
}

void IMGUI_GRAPHNODE_NAMESPACE::NodeGraphSetView(ImVec2 viewport, float scale, bool auto_fit, bool center)
{
    auto& cache = g_ctx.graph_caches[g_ctx.lastid];
    cache.viewport = viewport;
    cache.viewScale = ImClamp(scale, 0.65f, 2.0f);
    cache.autoFit = auto_fit;
    cache.center = center;
}

void IMGUI_GRAPHNODE_NAMESPACE::NodeGraphAddNode(char const * id)
{
    ImVec4 const color = ImGui::GetStyle().Colors[ImGuiCol_Text];
    ImVec4 const fillcolor = ImVec4(0.f, 0.f, 0.f, 0.f);
    NodeGraphAddNode(id, color, fillcolor);
}

void IMGUI_GRAPHNODE_NAMESPACE::NodeGraphAddNode(char const * id, ImVec4 const & color, ImVec4 const & fillcolor)
{
    NodeGraphAddNodeSized(id, ImVec2(0.0f, 0.0f), color, fillcolor);
}

void IMGUI_GRAPHNODE_NAMESPACE::NodeGraphAddNodeSized(char const* id, ImVec2 size, ImVec4 const& color, ImVec4 const& fillcolor)
{
    auto & cache = g_ctx.graph_caches[g_ctx.lastid];
    IM_ASSERT(g_ctx.gvgraph != nullptr);
    Agnode_t * const n = agnode(g_ctx.gvgraph, ImGuiIDToString(id), 1);
    IM_ASSERT(n != nullptr);
    IMGUI_GRAPHNODE_CREATE_LABEL_ALLOCA(text, id);
    auto const color_str = ImVec4ColorToString(color);
    auto const fillcolor_str = ImVec4ColorToString(fillcolor);
    agsafeset(n, (char *)"label", text, "");
    agsafeset(n, (char *)"color", color_str, "");
    agsafeset(n, (char *)"fillcolor", fillcolor_str, "");
    if (size.x > 0.0f && size.y > 0.0f)
    {
        char width[32];
        char height[32];
        snprintf(width, sizeof(width), "%.4f", size.x);
        snprintf(height, sizeof(height), "%.4f", size.y);
        agsafeset(n, (char*)"width", width, "");
        agsafeset(n, (char*)"height", height, "");
        agsafeset(n, (char*)"fixedsize", (char*)"true", "");
        agsafeset(n, (char*)"shape", (char*)"box", "");
    }

    cache.graphid_current += id;
    cache.graphid_current += color_str;
    cache.graphid_current += fillcolor_str;
    cache.graphid_current += std::to_string(size.x);
    cache.graphid_current += std::to_string(size.y);

    ImGuiID const imid = ImGui::GetID(id);
    auto const it = cache.graph.nodesBB.find(imid);
    ImRect const bb = it != cache.graph.nodesBB.end() ? it->second : ImRect();
    ImGui::ItemAdd(bb, imid);
}

void IMGUI_GRAPHNODE_NAMESPACE::NodeGraphSetRuntimeNodeStyle(const ImGuiGraphNodeRuntimeNodeStyle& style)
{
    if (!style.id)
        return;

    auto& cache = g_ctx.graph_caches[g_ctx.lastid];
    ImGuiID const imid = ImGui::GetID(style.id);
    auto& state = cache.runtimeNodeStyles[imid];
    state.id = style.id ? style.id : "";
    state.title = style.title ? style.title : "";
    state.tooltip = style.tooltip ? style.tooltip : "";
    state.inputLabel = style.inputLabel ? style.inputLabel : "";
    state.outputLabel = style.outputLabel ? style.outputLabel : "";
    state.metadata = style.metadata ? style.metadata : "";
    state.style = style;
    state.style.id = state.id.c_str();
    state.style.title = state.title.empty() ? nullptr : state.title.c_str();
    state.style.tooltip = state.tooltip.empty() ? nullptr : state.tooltip.c_str();
    state.style.inputLabel = state.inputLabel.empty() ? nullptr : state.inputLabel.c_str();
    state.style.outputLabel = state.outputLabel.empty() ? nullptr : state.outputLabel.c_str();
    state.style.metadata = state.metadata.empty() ? nullptr : state.metadata.c_str();

    ApplyRuntimeNodeStyleToGraph(cache, style);
    AppendRuntimeNodeStyleGraphId(cache, style);
}

void IMGUI_GRAPHNODE_NAMESPACE::NodeGraphAddEdge(char const * id, char const * node_id_a, char const * node_id_b)
{
    ImVec4 const color = ImGui::GetStyle().Colors[ImGuiCol_Text];
    NodeGraphAddEdge(id, node_id_a, node_id_b, color);
}

void IMGUI_GRAPHNODE_NAMESPACE::NodeGraphAddEdge(char const * id, char const * node_id_a, char const * node_id_b, ImVec4 const & color)
{
    auto & cache = g_ctx.graph_caches[g_ctx.lastid];
    IM_ASSERT(g_ctx.gvgraph != nullptr);
    Agnode_t * const a = agnode(g_ctx.gvgraph, ImGuiIDToString(node_id_a), 0);
    Agnode_t * const b = agnode(g_ctx.gvgraph, ImGuiIDToString(node_id_b), 0);
    IM_ASSERT(a != nullptr);
    IM_ASSERT(b != nullptr);
    Agedge_t * const e = agedge(g_ctx.gvgraph, a, b, ImGuiIDToString(id), 1);
    IM_ASSERT(e != nullptr);
    auto const color_str = ImVec4ColorToString(color);
    agsafeset(e, (char *)"label", (char *)"", "");
    ImGuiID const imid = ImGui::GetID(id, ImGui::FindRenderedTextEnd(id));
    char identifier[16];
    sprintf(identifier, "#%x", imid);
    // graphviz library doesn't serialize the edge's identifier, so we use the
    // color field to store the ImGuiID, which will later be used to retrieve
    // the edge's properties.
    agsafeset(e, (char *)"color", identifier, "");
    cache.edgeIdToInfo[imid] = ImGuiGraphNode_EdgeInfo { ImGui::GetColorU32(color) };

    cache.graphid_current += id;
    cache.graphid_current += node_id_a;
    cache.graphid_current += node_id_b;
    cache.graphid_current += color_str;

    ImGui::ItemAdd(ImRect(), imid);
    auto const it = cache.graph.edgesRectangle.find(imid);
    if (it != cache.graph.edgesRectangle.end())
    {
        for (auto const & rect : it->second)
        {
            // Uncomment to draw edge bouding boxes
            //ImVec2 lines[] { rect.a, rect.b, rect.c, rect.d };
            //ImGui::GetWindowDrawList()->AddPolyline(lines, 4, IM_COL32(255, 0, 0, 255), ImDrawFlags_Closed, 2.0f);

            if (IsPointInRectangle(rect.a, rect.b, rect.c, rect.d, ImGui::GetIO().MousePos))
            {
                GImGui->LastItemData.StatusFlags |= ImGuiItemStatusFlags_HoveredRect;
                break;
            }
        }
    }
}

int ImGuiGraphNodeFillDrawNodeBuffer(ImGuiGraphNode_Graph & graph, ImGuiGraphNode_DrawNode * drawnodes, ImVec2 cursor_pos, float ppu)
{
    int const count = (int)graph.nodes.size();

    if (drawnodes)
    {
        constexpr int num_segments = IMGUI_GRAPHNODE_DRAW_NODE_PATH_COUNT - 1;
        static_assert(num_segments > 0, "");
        float a_min = 0.f;
        float a_max = (IM_PI * 2.0f) * ((float)num_segments - 1.0f) / (float)num_segments;

        for (int i = 0; i < count; ++i)
        {
            ImGuiGraphNode_Node const & node = graph.nodes[i];
            ImVec2 const textsize = ImGui::CalcTextSize(node.label.c_str());

            for (int j = 0; j <= num_segments; j++)
            {
                const float a = a_min + ((float)j / (float)num_segments) * (a_max - a_min);
                drawnodes[i].path[j].x = cursor_pos.x + (node.pos.x + ImCos(a) * node.size.x / 2.f) * ppu;
                drawnodes[i].path[j].y = cursor_pos.y + ((graph.size.y - node.pos.y) + ImSin(a) * node.size.y / 2.f) * ppu;
            }
            drawnodes[i].textpos.x = cursor_pos.x + node.pos.x * ppu - textsize.x / 2.f;
            drawnodes[i].textpos.y = cursor_pos.y + (graph.size.y - node.pos.y) * ppu - textsize.y / 2.f;
            drawnodes[i].text = node.label.c_str();
            drawnodes[i].color = node.color;
            drawnodes[i].fillcolor = node.fillcolor;
            drawnodes[i].id = GraphvizNodeNameToImGuiID(node.name);

            ImRect const bb(
                cursor_pos.x + (node.pos.x - node.size.x / 2.f) * ppu,
                cursor_pos.y + ((graph.size.y - node.pos.y) - node.size.y / 2.f) * ppu,
                cursor_pos.x + (node.pos.x + node.size.x / 2.f) * ppu,
                cursor_pos.y + ((graph.size.y - node.pos.y) + node.size.y / 2.f) * ppu
            );
            ImGuiID const imid = GraphvizNodeNameToImGuiID(node.name);
            graph.nodesBB[imid] = bb;
        }
    }
    return count;
}

int ImGuiGraphNodeFillDrawEdgeBuffer(ImGuiGraphNode_Graph & graph, ImGuiGraphNode_DrawEdge * drawedges, ImVec2 cursor_pos, float ppu)
{
    int const count = (int)graph.edges.size();

    if (drawedges)
    {
        constexpr int points_count = IMGUI_GRAPHNODE_DRAW_EDGE_PATH_COUNT;
        static_assert(points_count > 1, "");

        for (int i = 0; i < count; ++i)
        {
            ImGuiGraphNode_Edge const & edge = graph.edges[i];
            ImVec2 const textsize = ImGui::CalcTextSize(edge.label.c_str());

            for (size_t j = 0; j < (edge.points.size() - 1); ++j)
            {
                ImVec2 const p1(
                    cursor_pos.x + edge.points[j].x * ppu,
                    cursor_pos.y + (graph.size.y - edge.points[j].y) * ppu
                );
                ImVec2 const p2(
                    cursor_pos.x + edge.points[j + 1].x * ppu,
                    cursor_pos.y + (graph.size.y - edge.points[j + 1].y) * ppu
                );
                ImVec2 const dir(p2.x - p1.x, p2.y - p1.y);
                ImVec2 left(-dir.y, dir.x);
                ImVec2 right(dir.y, -dir.x);
                float const magLeft = ImSqrt(left.x * left.x + left.y * left.y);
                float const magRight = ImSqrt(right.x * right.x + right.y * right.y);

                left.x /= magLeft;
                left.y /= magLeft;
                right.x /= magRight;
                right.y /= magRight;

                constexpr float k = 3.f;
                ImVec2 const a(p1.x + left.x * k, p1.y + left.y * k);
                ImVec2 const b(p1.x + right.x * k, p1.y + right.y * k);
                ImVec2 const c(p2.x + right.x * k, p2.y + right.y * k);
                ImVec2 const d(p2.x + left.x * k, p2.y + left.y * k);

                graph.edgesRectangle[edge.id].push_back({ a, b, c, d });
            }
            if (edge.points.size() < 2)
                continue;
            for (int x = 0; x < points_count; ++x)
            {
                ImVec2 p = GraphvizSplinePoint(edge.points, x / static_cast<float>(points_count - 1));
                p.y = graph.size.y - p.y;
                p.x *= ppu;
                p.y *= ppu;
                p.x += cursor_pos.x;
                p.y += cursor_pos.y;
                drawedges[i].path[x] = p;
            }
            drawedges[i].textpos.x = cursor_pos.x + edge.labelPos.x * ppu - textsize.x / 2.f;
            drawedges[i].textpos.y = cursor_pos.y + (graph.size.y - edge.labelPos.y) * ppu - textsize.y / 2.f;
            drawedges[i].text = edge.label.c_str();
            drawedges[i].color = edge.color;

            ImVec2 const lastpoint = drawedges[i].path[points_count - 1];
            float dirx = lastpoint.x - drawedges[i].path[points_count - 2].x;
            float diry = lastpoint.y - drawedges[i].path[points_count - 2].y;
            float const mag = ImSqrt(dirx * dirx + diry * diry);
            if (mag <= 0.0001f)
                continue;
            float const mul1 = ppu * 0.1f;
            float const mul2 = ppu * 0.0437f;

            dirx /= mag;
            diry /= mag;
            drawedges[i].arrow1.x = lastpoint.x - dirx * mul1 - diry * mul2;
            drawedges[i].arrow1.y = lastpoint.y - diry * mul1 + dirx * mul2;
            drawedges[i].arrow2.x = lastpoint.x - dirx * mul1 + diry * mul2;
            drawedges[i].arrow2.y = lastpoint.y - diry * mul1 - dirx * mul2;
            drawedges[i].arrow3 = lastpoint;
        }
    }
    return count;
}

void IMGUI_GRAPHNODE_NAMESPACE::EndNodeGraph()
{
    auto & cache = g_ctx.graph_caches[g_ctx.lastid];
    constexpr float kMinReadableGraphScale = 0.65f;
    float ppu = cache.pixel_per_unit * ImClamp(cache.viewScale, kMinReadableGraphScale, 2.0f);
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImDrawList * const drawlist = ImGui::GetWindowDrawList();
    if (g_ctx.gvgraph == nullptr)
    {
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
        return;
    }

    if (cache.graphid_current != cache.graphid_previous)
    {
        if (!cache.raw_dot_current.empty() && !cache.raw_dot_loaded)
        {
            agclose(g_ctx.gvgraph);
            g_ctx.gvgraph = agmemread(cache.raw_dot_current.c_str());
            cache.raw_dot_loaded = g_ctx.gvgraph != nullptr;
            if (!g_ctx.gvgraph)
                g_ctx.gvgraph = agopen(const_cast<char*>("g"), Agdirected, 0);

            for (auto& [_, state] : cache.runtimeNodeStyles)
                ApplyRuntimeNodeStyleToGraph(cache, state.style);
        }
        ImGuiGraphNodeRenderGraphLayout(cache);
        cache.graphid_previous = cache.graphid_current;
        cache.raw_dot_previous = cache.raw_dot_current;
        cache.cursor_previous.x = cursor_pos.x - 1; // force recompute draw buffers
    }
    cache.graphid_current.clear();
    agclose(g_ctx.gvgraph);
    g_ctx.gvgraph = nullptr;

    if (cache.autoFit && cache.viewport.x > 1.0f && cache.viewport.y > 1.0f &&
        cache.graph.size.x > 0.0f && cache.graph.size.y > 0.0f)
    {
        constexpr float padding = 36.0f;
        const float graphW = cache.graph.size.x * cache.pixel_per_unit;
        const float graphH = cache.graph.size.y * cache.pixel_per_unit;
        const float fitX = (cache.viewport.x - padding) / graphW;
        const float fitY = (cache.viewport.y - padding) / graphH;
        ppu = cache.pixel_per_unit * ImClamp(ImMin(fitX, fitY), kMinReadableGraphScale, 1.0f);
    }
    if (cache.center && cache.viewport.x > 1.0f && cache.viewport.y > 1.0f)
    {
        const ImVec2 graphSize {cache.graph.size.x * ppu, cache.graph.size.y * ppu};
        if (graphSize.x < cache.viewport.x)
            cursor_pos.x += (cache.viewport.x - graphSize.x) * 0.5f;
        if (graphSize.y < cache.viewport.y)
            cursor_pos.y += (cache.viewport.y - graphSize.y) * 0.5f;
    }

    cache.cursor_current = cursor_pos;
    if (cache.cursor_current.x != cache.cursor_previous.x || cache.cursor_current.y != cache.cursor_previous.y ||
        cache.draw_pixel_per_unit != ppu)
    {
        cache.drawnodes.resize(ImGuiGraphNodeFillDrawNodeBuffer(cache.graph, nullptr, cursor_pos, ppu));
        ImGuiGraphNodeFillDrawNodeBuffer(cache.graph, cache.drawnodes.data(), cursor_pos, ppu);
        cache.drawedges.resize(ImGuiGraphNodeFillDrawEdgeBuffer(cache.graph, nullptr, cursor_pos, ppu));
        ImGuiGraphNodeFillDrawEdgeBuffer(cache.graph, cache.drawedges.data(), cursor_pos, ppu);
        cache.cursor_previous = cache.cursor_current;
        cache.draw_pixel_per_unit = ppu;
    }

    // Vultra draws node bodies and labels itself so Graphviz can focus on layout and routing.
    for (auto const & edge : cache.drawedges)
    {
        drawlist->AddPolyline(edge.path, IMGUI_GRAPHNODE_DRAW_EDGE_PATH_COUNT, edge.color, ImDrawFlags_None, 1.f);
        drawlist->AddTriangleFilled(edge.arrow1, edge.arrow2, edge.arrow3, edge.color);
    }
    cache.runtimeNodeDrawCount = 0;
    for (const auto& node : cache.drawnodes)
    {
        const auto id = node.id;
        auto rectIt = cache.graph.nodesBB.find(id);
        if (rectIt == cache.graph.nodesBB.end())
            continue;
        auto styleIt = cache.runtimeNodeStyles.find(id);
        if (styleIt != cache.runtimeNodeStyles.end())
        {
            if (DrawRuntimeNodeBody(styleIt->second.style, rectIt->second))
                cache.runtimeTextureDoubleClicked.push_back(id);
        }
        else
        {
            DrawRuntimeNodeFallback(node, rectIt->second);
        }
        ++cache.runtimeNodeDrawCount;
    }
    ImGui::SetCursorScreenPos(cursor_pos);
    const ImVec2 graphSize {cache.graph.size.x * ppu, cache.graph.size.y * ppu};
    const ImVec2 dummySize {
        cache.viewport.x > 1.0f ? ImMax(cache.viewport.x, graphSize.x) : graphSize.x,
        cache.viewport.y > 1.0f ? ImMax(cache.viewport.y, graphSize.y) : graphSize.y
    };
    ImGui::Dummy(dummySize);
}

bool IMGUI_GRAPHNODE_NAMESPACE::GetNodeGraphNodeRect(char const* id, ImVec2* min, ImVec2* max)
{
    auto it = g_ctx.graph_caches.find(g_ctx.lastid);
    if (it == g_ctx.graph_caches.end())
        return false;

    const ImGuiID imid = ImGui::GetID(id);
    auto rectIt = it->second.graph.nodesBB.find(imid);
    if (rectIt == it->second.graph.nodesBB.end())
        return false;

    if (min)
        *min = rectIt->second.Min;
    if (max)
        *max = rectIt->second.Max;
    return true;
}

bool IMGUI_GRAPHNODE_NAMESPACE::WasRuntimeNodeTextureDoubleClicked(char const* id)
{
    if (!id)
        return false;

    auto it = g_ctx.graph_caches.find(g_ctx.lastid);
    if (it == g_ctx.graph_caches.end())
        return false;

    const ImGuiID imid = ImGui::GetID(id);
    return std::find(it->second.runtimeTextureDoubleClicked.begin(),
                     it->second.runtimeTextureDoubleClicked.end(),
                     imid) != it->second.runtimeTextureDoubleClicked.end();
}
