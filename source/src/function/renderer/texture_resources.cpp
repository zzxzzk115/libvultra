#include "vultra/function/renderer/texture_resources.hpp"

namespace vultra
{
    namespace gfx
    {
        bool operator==(const TextureResources& a, const TextureResources& b)
        {
            return a.size() == b.size() && std::equal(a.cbegin(), a.cend(), b.begin());
        }
    } // namespace gfx
} // namespace vultra