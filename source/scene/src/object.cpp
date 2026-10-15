#include <vultra/scene/object.hpp>

namespace vultra
{
    Object::Object() :
        m_Id(ObjectId::generate())
    {
    }
} // namespace vultra
