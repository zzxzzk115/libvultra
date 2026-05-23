#include "vultra/function/scene/vscn_writer.hpp"

#include <sstream>
#include <unordered_map>

namespace vultra
{
    static void writeNode(std::ostringstream&                        out,
                          const SceneNode&                           node,
                          int                                        nodeId,
                          int                                        parentId,
                          std::unordered_map<const SceneNode*, int>& ids,
                          int&                                       nextId)
    {
        ids[&node] = nodeId;

        out << "[node id=" << nodeId;
        if (!node.name.empty())
            out << " name=\"" << node.name << "\"";
        out << " parent=" << parentId;
        out << " uuid=\"" << node.id.toString() << "\"";
        if (!node.prefabUri.empty())
            out << " prefab=\"" << node.prefabUri << "\"";
        out << "]\n";

        for (const auto& p : node.properties)
        {
            out << p.component << "/" << p.field << " = " << p.value << "\n";
        }

        out << "\n";

        for (const auto& ch : node.children)
        {
            const int cid = nextId++;
            writeNode(out, *ch, cid, nodeId, ids, nextId);
        }
    }

    std::string VscnWriter::writeToText(const SceneDocument& doc)
    {
        std::ostringstream out;

        out << "[vscn]\n";
        out << "version = " << doc.version << "\n";
        out << "root    = " << (doc.syntheticRoot ? 0 : 1) << "\n\n";

        if (!doc.root)
            return out.str();

        std::unordered_map<const SceneNode*, int> ids;
        int                                       nextId = 1;
        if (doc.syntheticRoot)
        {
            for (const auto& child : doc.root->children)
            {
                const int nodeId = nextId++;
                writeNode(out, *child, nodeId, 0, ids, nextId);
            }
        }
        else
        {
            nextId = 2;
            writeNode(out, *doc.root, 1, 0, ids, nextId);
        }

        return out.str();
    }
} // namespace vultra
