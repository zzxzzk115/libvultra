#include "vultra/core/builtin/builtin_resources.hpp"

#include <vfilesystem/interfaces/ifile.hpp>
#include <vfilesystem/interfaces/ifilesystem.hpp>

namespace vultra::builtin
{
    namespace
    {
        // Function-local static: safe to assign from a static initializer in another TU
        // (the per-binary builtin_pack_mount.cpp self-registers at startup).
        std::shared_ptr<vfilesystem::IFileSystem>& source()
        {
            static std::shared_ptr<vfilesystem::IFileSystem> s;
            return s;
        }
    } // namespace

    void setSource(std::shared_ptr<vfilesystem::IFileSystem> backend) { source() = std::move(backend); }

    bool hasSource() { return static_cast<bool>(source()); }

    bool read(std::string_view logicalPath, std::vector<std::byte>& out)
    {
        if (!source())
            return false;

        auto opened = source()->open(logicalPath, vfilesystem::FileMode::eRead);
        if (!opened)
            return false;

        out = opened.value()->readAllBytes();
        return true;
    }
} // namespace vultra::builtin
