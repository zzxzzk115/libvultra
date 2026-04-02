#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/interfaces/ibuffer_backend.hpp"

namespace vultra::rhi
{
    Buffer::Buffer(Buffer&& other) noexcept : m_Impl(std::move(other.m_Impl)) {}

    Buffer::~Buffer() { destroy(); }

    Buffer& Buffer::operator=(Buffer&& rhs) noexcept
    {
        if (this != &rhs)
        {
            destroy();
            m_Impl = std::move(rhs.m_Impl);
        }
        return *this;
    }

    Buffer::operator bool() const { return m_Impl && m_Impl->isValid(); }

    std::uintptr_t Buffer::getHandle() const
    {
        assert(m_Impl);
        return m_Impl->getHandle();
    }

    std::uintptr_t Buffer::getNativeHandle() const { return getHandle(); }

    uint64_t Buffer::getSize() const
    {
        assert(m_Impl);
        return m_Impl->getSize();
    }

    void* Buffer::map()
    {
        assert(m_Impl);
        return m_Impl->map();
    }

    Buffer& Buffer::unmap()
    {
        assert(m_Impl);
        m_Impl->unmap();
        return *this;
    }

    Buffer& Buffer::flush(const uint64_t offset, const uint64_t size)
    {
        assert(m_Impl);
        m_Impl->flush(offset, size);
        return *this;
    }

    BarrierScope Buffer::getBarrierScope() const
    {
        assert(m_Impl);
        return m_Impl->getLastScope();
    }

    void Buffer::setBarrierScope(BarrierScope scope)
    {
        assert(m_Impl);
        m_Impl->setLastScope(scope);
    }

    Buffer::Buffer(std::unique_ptr<IBufferBackend> impl) : m_Impl(std::move(impl)) {}

    void Buffer::destroy() noexcept { m_Impl.reset(); }
} // namespace vultra::rhi
