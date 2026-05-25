#include "imgui_graphnode.h"
#include "imgui_graphnode_internal.h"
#include "imgui_internal.h"

#if defined(_MSC_VER)
#include <malloc.h>
#endif

ImGuiGraphNodeContext g_ctx;

#if defined(_WIN32)
static char* ImGuiGraphNode_StrSep(char** stringp, const char* delim)
{
    if (!stringp || !*stringp)
        return nullptr;

    char* start = *stringp;
    char* p = start;
    while (*p)
    {
        for (const char* d = delim; *d; ++d)
        {
            if (*p == *d)
            {
                *p = '\0';
                *stringp = p + 1;
                return start;
            }
        }
        ++p;
    }

    *stringp = nullptr;
    return start;
}
#else
#define ImGuiGraphNode_StrSep strsep
#endif

ImGuiGraphNode_ShortString<32> ImGuiIDToString(char const * id)
{
    ImGuiGraphNode_ShortString<32> str;

    sprintf(str.buf, "%u", ImGui::GetID(id));
    return str;
}

ImGuiGraphNode_ShortString<16> ImVec4ColorToString(ImVec4 const & color)
{
    ImGuiGraphNode_ShortString<16> str;
    ImU32 const rgba = ImGui::ColorConvertFloat4ToU32(color);

    sprintf(str.buf, "#%x", rgba);
    return str;
}

ImU32 ImGuiGraphNode_StringToU32Color(char const * color)
{
    if (color == nullptr)
        return IM_COL32_WHITE;
    if (strcmp(color, "orangered") == 0)
        return IM_COL32(255, 69, 0, 235);
    if (strcmp(color, "yellowgreen") == 0)
        return IM_COL32(154, 205, 50, 235);
    if (strcmp(color, "skyblue") == 0)
        return IM_COL32(135, 206, 235, 255);
    if (strcmp(color, "lightsteelblue") == 0)
        return IM_COL32(176, 196, 222, 255);
    if (strcmp(color, "lightgray") == 0 || strcmp(color, "lightgrey") == 0)
        return IM_COL32(211, 211, 211, 255);
    if (strcmp(color, "orange") == 0)
        return IM_COL32(255, 165, 0, 255);
    if (color[0] != '#')
        return IM_COL32_WHITE;

    ImU32 rgba;

    sscanf(color, "#%x", &rgba);
    return rgba;
}

ImVec4 ImGuiGraphNode_StringToImVec4Color(char const * color)
{
    return ImGui::ColorConvertU32ToFloat4(ImGuiGraphNode_StringToU32Color(color));
}

char * ImGuiGraphNode_ReadToken(char ** stringp)
{
    if (*stringp && **stringp == '"')
    {
        *stringp += 1;
        char * token = ImGuiGraphNode_StrSep(stringp, "\"");
        ImGuiGraphNode_StrSep(stringp, " ");
        return token;
    }
    return ImGuiGraphNode_StrSep(stringp, " ");
}

char * ImGuiGraphNode_ReadLine(char ** stringp)
{
    return ImGuiGraphNode_StrSep(stringp, "\n");
}

bool ImGuiGraphNode_ReadGraphFromMemory(ImGuiGraphNodeContextCache & cache, char const * data, size_t size)
{
    if (data == nullptr || size == 0)
        return false;

    char * copy = static_cast<char *>(alloca(sizeof(*copy) * size + 1));
    char * line = nullptr;

    memcpy(copy, data, size);
    copy[size] = '\0';
    while ((line = ImGuiGraphNode_ReadLine(&copy)) != nullptr)
    {
        char * token = ImGuiGraphNode_ReadToken(&line);
        if (token == nullptr || token[0] == '\0')
            continue;

        if (strcmp(token, "graph") == 0)
        {
            char* scale = ImGuiGraphNode_ReadToken(&line);
            char* width = ImGuiGraphNode_ReadToken(&line);
            char* height = ImGuiGraphNode_ReadToken(&line);
            if (!scale || !width || !height)
                continue;
            cache.graph.scale = atof(scale);
            cache.graph.size.x = atof(width);
            cache.graph.size.y = atof(height);
        }
        else if (strcmp(token, "node") == 0)
        {
            ImGuiGraphNode_Node node;

            char* name = ImGuiGraphNode_ReadToken(&line);
            char* x = ImGuiGraphNode_ReadToken(&line);
            char* y = ImGuiGraphNode_ReadToken(&line);
            char* w = ImGuiGraphNode_ReadToken(&line);
            char* h = ImGuiGraphNode_ReadToken(&line);
            char* label = ImGuiGraphNode_ReadToken(&line);
            if (!name || !x || !y || !w || !h || !label)
                continue;
            node.name = name;
            node.pos.x = atof(x);
            node.pos.y = atof(y);
            node.size.x = atof(w);
            node.size.y = atof(h);
            node.label = label;
            ImGuiGraphNode_ReadToken(&line); // style
            ImGuiGraphNode_ReadToken(&line); // shape
            char* color = ImGuiGraphNode_ReadToken(&line);
            char* fillcolor = ImGuiGraphNode_ReadToken(&line);
            node.color = color ? ImGuiGraphNode_StringToU32Color(color) : IM_COL32_WHITE;
            node.fillcolor = fillcolor ? ImGuiGraphNode_StringToU32Color(fillcolor) : 0;
            cache.graph.nodes.push_back(node);
        }
        else if (strcmp(token, "edge") == 0)
        {
            ImGuiGraphNode_Edge edge;

            edge.tail = ImGuiGraphNode_ReadToken(&line);
            edge.head = ImGuiGraphNode_ReadToken(&line);
            char* pointCount = ImGuiGraphNode_ReadToken(&line);
            if (!pointCount)
                continue;
            int const n = atoi(pointCount);
            if (n < 2)
                continue;
            edge.points.resize(n);
            for (int i = 0; i < n; ++i)
            {
                edge.points[i].x = atof(ImGuiGraphNode_ReadToken(&line));
                edge.points[i].y = atof(ImGuiGraphNode_ReadToken(&line));
            }

            char const * s1 = ImGuiGraphNode_ReadToken(&line);
            char const * s2 = ImGuiGraphNode_ReadToken(&line);
            char const * s3 = ImGuiGraphNode_ReadToken(&line);
            char const * s4 = ImGuiGraphNode_ReadToken(&line); (void)s4; // style
            char const * s5 = ImGuiGraphNode_ReadToken(&line);
            char const * identifier = nullptr;

            if (s3)
            {
                edge.label = s1;
                edge.labelPos.x = atof(s2);
                edge.labelPos.y = atof(s3);
                identifier = s5 ? s5 + 1 : nullptr;
            }
            else
            {
                identifier = s2 ? s2 + 1 : nullptr;
            }
            if (identifier == nullptr || identifier[-1] != '#')
                edge.id = ImGui::GetID((edge.tail + "->" + edge.head + ":" + std::to_string(cache.graph.edges.size())).c_str());
            else
                edge.id = strtoul(identifier, nullptr, 16);

            // Edge ImGuiID is stored in the color property.
            // It is used to access edge info, as graphviz doesn't serialize
            // the edge's identifier. The actual color is then retrieve from
            // the context cache.
            auto const it = cache.edgeIdToInfo.find(edge.id);
            if (it != cache.edgeIdToInfo.end())
                edge.color = it->second.color;
            else if (s5)
                edge.color = ImGuiGraphNode_StringToU32Color(s5);
            else if (s2 && s3 == nullptr)
                edge.color = ImGuiGraphNode_StringToU32Color(s2);
            else
                edge.color = IM_COL32(96, 160, 220, 220);

            cache.graph.edges.push_back(edge);
        }
        else if (strcmp(token, "stop") == 0)
        {
            return true;
        }
        else
        {
            continue;
        }
    }
    return true;
}

char const * ImGuiGraphNode_GetEngineNameFromLayoutEnum(ImGuiGraphNodeLayout layout)
{
    switch (layout)
    {
        case ImGuiGraphNodeLayout_Circo: return "circo";
        case ImGuiGraphNodeLayout_Dot: return "dot";
        case ImGuiGraphNodeLayout_Fdp: return "fdp";
        case ImGuiGraphNodeLayout_Neato: return "neato";
        case ImGuiGraphNodeLayout_Osage: return "osage";
        case ImGuiGraphNodeLayout_Sfdp: return "sfdp";
        case ImGuiGraphNodeLayout_Twopi: return "twopi";
        default:
            IM_ASSERT(false);
            return "";
    }
}

float ImGuiGraphNode_BSplineVec2ComputeK(ImVec2 const * p, float const * t, int i, int k, float x)
{
    auto const f = [](float const * t, int i, int k, float x) -> float
    {
        if (t[i + k] == t[i])
            return 0.f;
        return (x - t[i]) / (t[i + k] - t[i]);
    };
    auto g = ImGuiGraphNode_BSplineVec2ComputeK;
    if (k == 0)
        return (x < t[i] || x >= t[i + 1]) ? 0.f : 1.f;
    return f(t, i, k, x) * g(p, t, i, k - 1, x) + (1.f - f(t, i + 1, k, x)) * g(p, t, i + 1, k - 1, x);
}

ImVec2 ImGuiGraphNode_BSplineVec2(ImVec2 const * p, int m, int n, float x)
{
    if (n > (m - 2)) n = m - 2;
    int const knotscount = m + n + 1;
    float * const knots = (float *)alloca(knotscount * sizeof(*knots));
    int i = 0;

    for (; i <= n; ++i) knots[i] = 0.f;
    for (; i < m; ++i) knots[i] = i / (float)(m + n);
    for (; i < knotscount; ++i) knots[i] = 1.f;

    ImVec2 result(0.f, 0.f);

    for (i = 0; i < m; ++i)
    {
        float const k = ImGuiGraphNode_BSplineVec2ComputeK(p, knots, i, n, x);

        result.x += p[i].x * k;
        result.y += p[i].y * k;
    }
    return result;
}

ImVec2 ImGuiGraphNode_BSplineVec2(ImVec2 const * points, int count, float x)
{
    return ImGuiGraphNode_BSplineVec2(points, count, 3, ImClamp(x, 0.f, 0.9999f));
}

#define IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_1(_line, _cell) \
    ImGuiGraphNode_BinomialCoefficient(_line, _cell), \

#define IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_2(_line, _cell) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_1(_line, _cell) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_1(_line, _cell + 1)

#define IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_4(_line, _cell) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_2(_line, _cell) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_2(_line, _cell + 2)

#define IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_8(_line, _cell) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_4(_line, _cell) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_4(_line, _cell + 4)

#define IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_16(_line, _cell) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_8(_line, _cell) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_8(_line, _cell + 8)

#define IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_1(_line) \
    { IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_CELL_16(_line, 0) },

#define IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_2(_line) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_1(_line) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_1(_line + 1)

#define IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_4(_line) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_2(_line) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_2(_line + 2)

#define IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_8(_line) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_4(_line) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_4(_line + 4)

#define IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_16(_line) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_8(_line) \
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_8(_line + 8)

constexpr int ImGuiGraphNode_BinomialCoefficient(int n, int k)
{
    return (n == 0 || k == 0 || n == k) ? 1 : ImGuiGraphNode_BinomialCoefficient(n - 1, k - 1) + ImGuiGraphNode_BinomialCoefficient(n - 1, k);
}

constexpr int ImGuiGraphNode_BinomialCoefficient_Table[16][16] =
{
    IMGUIGRAPHNODE_BINOMIALCOEFFICIENT_TABLE_LINE_16(0)
};

int ImGuiGraphNode_BinomialCoefficientTable(int n, int k)
{
    if (n >= 0 && n < 16 && k >= 0 && k < 16)
    {
        return ImGuiGraphNode_BinomialCoefficient_Table[n][k];
    }
    else
    {
        return ImGuiGraphNode_BinomialCoefficient(n, k);
    }
}

ImVec2 ImGuiGraphNode_BezierVec2(ImVec2 const * points, int count, float x)
{
    ImVec2 result(0.f, 0.f);

    for (int i = 0; i < count; ++i)
    {
        float const k = ImGuiGraphNode_BinomialCoefficientTable(count - 1, i) * ImPow(1 - x, count - i - 1) * ImPow(x, i);

        result.x += points[i].x * k;
        result.y += points[i].y * k;
    }
    return result;
}

void ImGuiGraphNodeRenderGraphLayout(ImGuiGraphNodeContextCache & cache)
{
    char * data = nullptr;
    size_t size = 0;
    char const * const engine = ImGuiGraphNode_GetEngineNameFromLayoutEnum(cache.layout);
    int ok = 0;

    cache.graph = ImGuiGraphNode_Graph();
    if (g_ctx.gvcontext == nullptr || g_ctx.gvgraph == nullptr || engine == nullptr)
        return;
    agattr(g_ctx.gvgraph, AGEDGE, (char *)"dir", "none");
    ok = gvLayout(g_ctx.gvcontext, g_ctx.gvgraph, engine);
    if (ok != 0)
        return;
    ok = gvRenderData(g_ctx.gvcontext, g_ctx.gvgraph, "plain", &data, &size);
    if (ok == 0 && data != nullptr)
    {
        ImGuiGraphNode_ReadGraphFromMemory(cache, data, size);
        gvFreeRenderData(data);
    }
    gvFreeLayout(g_ctx.gvcontext, g_ctx.gvgraph);
}
