#pragma once

#include <string>
#include <vector>

namespace vultra
{
    // ---------------------------------------------------------------------
    // VSCN document (Scene Asset)
    //
    // - Textual scene asset representation (".vscn").
    // - Holds nodes and raw property strings.
    // - DOES NOT contain runtime World state.
    // ---------------------------------------------------------------------
    class VSceneDocument
    {
    public:
        struct Property
        {
            std::string component;
            std::string field;
            std::string value; // raw text RHS, trimmed, without comments
        };

        struct Node
        {
            int                   id     = 0;
            int                   parent = 0;
            std::string           name;
            std::vector<Property> properties;
        };

        int  version() const { return m_Version; }
        int  rootId() const { return m_RootId; }
        void setVersion(int v) { m_Version = v; }
        void setRootId(int id) { m_RootId = id; }

        std::vector<Node>&       nodes() { return m_Nodes; }
        const std::vector<Node>& nodes() const { return m_Nodes; }

        void clear();

    private:
        int               m_Version = 1;
        int               m_RootId  = 1;
        std::vector<Node> m_Nodes;
    };
} // namespace vultra
