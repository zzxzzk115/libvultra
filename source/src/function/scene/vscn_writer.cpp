#include "vultra/function/scene/vscn_writer.hpp"

#include <sstream>

namespace vultra
{
    std::string VSceneWriter::write(const VSceneDocument& doc)
    {
        std::stringstream ss;

        ss << "[vscn]\n";
        ss << "version = " << doc.version() << "\n";
        ss << "root    = " << doc.rootId() << "\n\n";

        for (const auto& n : doc.nodes())
        {
            ss << "[node id=" << n.id;
            if (!n.name.empty())
                ss << " name=\"" << n.name << "\"";
            ss << " parent=" << n.parent;
            ss << "]\n";

            for (const auto& p : n.properties)
            {
                ss << p.component << "/" << p.field << " = " << p.value << "\n";
            }

            ss << "\n";
        }

        return ss.str();
    }
} // namespace vultra
