#include "vultra/function/scene/vscn_reader.hpp"
#include "vultra/core/base/uuid.hpp"

#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace vultra
{
    static inline std::string trim_copy(std::string_view s)
    {
        size_t b = 0;
        while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
            ++b;
        size_t e = s.size();
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
            --e;
        return std::string(s.substr(b, e - b));
    }

    static inline bool starts_with(std::string_view s, std::string_view p)
    {
        return s.size() >= p.size() && s.substr(0, p.size()) == p;
    }

    static inline std::unordered_map<std::string, std::string> parse_attrs(std::string_view inside)
    {
        // inside: "node id=1 name=\"Root\" parent=0 prefab=\"x\" uuid=\"...\""
        std::unordered_map<std::string, std::string> out;

        auto skip_ws = [&](size_t& i) {
            while (i < inside.size() && std::isspace(static_cast<unsigned char>(inside[i])))
                ++i;
        };

        size_t i = 0;
        // first token (e.g. "node")
        skip_ws(i);
        while (i < inside.size() && !std::isspace(static_cast<unsigned char>(inside[i])))
            ++i;

        while (i < inside.size())
        {
            skip_ws(i);
            if (i >= inside.size())
                break;

            size_t k0 = i;
            while (i < inside.size() && inside[i] != '=' && !std::isspace(static_cast<unsigned char>(inside[i])))
                ++i;
            std::string key = std::string(inside.substr(k0, i - k0));
            skip_ws(i);
            if (i >= inside.size() || inside[i] != '=')
                break;
            ++i;
            skip_ws(i);

            std::string val;
            if (i < inside.size() && inside[i] == '"')
            {
                ++i;
                size_t v0 = i;
                while (i < inside.size() && inside[i] != '"')
                    ++i;
                val = std::string(inside.substr(v0, i - v0));
                if (i < inside.size() && inside[i] == '"')
                    ++i;
            }
            else
            {
                size_t v0 = i;
                while (i < inside.size() && !std::isspace(static_cast<unsigned char>(inside[i])))
                    ++i;
                val = std::string(inside.substr(v0, i - v0));
            }

            if (!key.empty())
                out[key] = val;
        }

        return out;
    }

    SceneDocument VscnReader::readFromText(std::string_view text, const std::filesystem::path& /*baseDir*/)
    {
        SceneDocument doc;

        std::unordered_map<int, std::unique_ptr<SceneNode>> nodes;
        std::unordered_map<int, int>                        parentOf;

        int         rootId        = -1;
        int         currentNodeId = -1;
        std::string currentSection;

        std::istringstream iss(text.data());
        std::string        line;

        while (std::getline(iss, line))
        {
            std::string t = trim_copy(line);
            if (t.empty() || t[0] == '#')
                continue;

            if (t.front() == '[' && t.back() == ']')
            {
                std::string inside = t.substr(1, t.size() - 2);
                inside             = trim_copy(inside);

                if (inside == "vscn")
                {
                    currentSection = "vscn";
                    currentNodeId  = -1;
                    continue;
                }

                if (starts_with(inside, "node"))
                {
                    auto attrs = parse_attrs(inside);

                    int id = 0;
                    if (auto it = attrs.find("id"); it != attrs.end())
                        id = std::stoi(it->second);
                    else
                        throw std::runtime_error("[node] missing id");

                    currentNodeId  = id;
                    currentSection = "node";

                    auto node = std::make_unique<SceneNode>();

                    // uuid
                    if (auto it = attrs.find("uuid"); it != attrs.end())
                    {
                        vbase::UUID tmp {};
                        vbase::try_parse_uuid(it->second.c_str(), tmp);
                        node->id = CoreUUID(tmp);
                    }
                    else
                    {
                        node->id = CoreUUIDHelper::createStandardUUID();
                    }

                    if (auto it = attrs.find("name"); it != attrs.end())
                        node->name = it->second;

                    if (auto it = attrs.find("prefab"); it != attrs.end())
                        node->prefabUri = it->second;

                    int parent = 0;
                    if (auto it = attrs.find("parent"); it != attrs.end())
                        parent = std::stoi(it->second);
                    parentOf[id] = parent;

                    nodes[id] = std::move(node);
                    continue;
                }

                // Unknown section, ignore
                currentSection = inside;
                currentNodeId  = -1;
                continue;
            }

            // key/value inside [vscn]
            if (currentSection == "vscn")
            {
                auto eq = t.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string key = trim_copy(std::string_view(t).substr(0, eq));
                std::string val = trim_copy(std::string_view(t).substr(eq + 1));
                if (key == "version")
                    doc.version = static_cast<uint32_t>(std::stoul(val));
                else if (key == "root")
                    rootId = std::stoi(val);
                continue;
            }

            // property line inside [node]
            if (currentSection == "node" && currentNodeId != -1)
            {
                auto eq = t.find('=');
                if (eq == std::string::npos)
                    continue;

                std::string lhs = trim_copy(std::string_view(t).substr(0, eq));
                std::string rhs = trim_copy(std::string_view(t).substr(eq + 1));

                auto slash = lhs.find('/');
                if (slash == std::string::npos)
                    continue;

                SceneProperty prop;
                prop.component = trim_copy(std::string_view(lhs).substr(0, slash));
                prop.field     = trim_copy(std::string_view(lhs).substr(slash + 1));
                prop.value     = rhs;

                nodes[currentNodeId]->properties.push_back(std::move(prop));
            }
        }

        if (nodes.empty())
            return doc;

        // Determine root
        if (rootId <= 0)
        {
            // fallback: first node whose parent is 0
            for (auto& [id, p] : parentOf)
            {
                if (p == 0)
                {
                    rootId = id;
                    break;
                }
            }
        }

        if (rootId <= 0 || nodes.find(rootId) == nodes.end())
        {
            // fallback: smallest id
            rootId = nodes.begin()->first;
        }

        // Build adjacency list: parentId -> [childIds]
        std::unordered_map<int, std::vector<int>> children;
        for (auto& [id, p] : parentOf)
        {
            children[p].push_back(id);
        }

        auto takeNode = [&](int id) -> std::unique_ptr<SceneNode> {
            auto it = nodes.find(id);
            if (it == nodes.end())
                return nullptr;
            auto ptr = std::move(it->second);
            nodes.erase(it);
            return ptr;
        };

        std::function<std::unique_ptr<SceneNode>(int)> build;
        build = [&](int id) -> std::unique_ptr<SceneNode> {
            auto n = takeNode(id);
            if (!n)
                return nullptr;
            for (int cid : children[id])
            {
                if (cid == id)
                    continue;
                if (auto c = build(cid))
                    n->children.push_back(std::move(c));
            }
            return n;
        };

        doc.root = build(rootId);

        // Attach any other root-level nodes (parent=0) under doc.root.
        for (int cid : children[0])
        {
            if (cid == rootId)
                continue;
            if (auto c = build(cid))
                doc.root->children.push_back(std::move(c));
        }

        return doc;
    }
} // namespace vultra
