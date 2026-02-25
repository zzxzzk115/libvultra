#include "vultra/function/scene/vscn_document.hpp"

namespace vultra
{
    void VSceneDocument::clear()
    {
        m_Version = 1;
        m_RootId  = 1;
        m_Nodes.clear();
    }
} // namespace vultra
