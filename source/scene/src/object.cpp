#include <vultra/scene/object.hpp>

#include <atomic>
#include <limits>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        std::atomic<uint64_t> nextObjectId {1};
    }

    Object::Object()
    {
        uint64_t value = nextObjectId.load(std::memory_order_relaxed);
        for (;;)
        {
            if (value == 0 || value == std::numeric_limits<uint64_t>::max())
            {
                throw std::overflow_error("Runtime object ID space exhausted");
            }
            if (nextObjectId.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
            {
                m_Id = {value};
                break;
            }
        }
    }
} // namespace vultra
