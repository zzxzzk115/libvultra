#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/file_dialog.hpp>
#include <vultra/platform/window.hpp>

#define NFD_THROWS_EXCEPTIONS
#include <nfd.hpp>

#include <stdexcept>
#include <string>

namespace vultra
{
    std::optional<std::filesystem::path> openModelDialog(const Window& owner)
    {
        const NFD::Guard      guard;
        const nfdfilteritem_t filter {"Static models", "gltf,glb,obj,fbx"};
        nfdwindowhandle_t     parent {};
        const auto            native = platform::nativeWindow(owner);
        if (native.type == VriWindowSystem_Xlib)
        {
            parent = {NFD_WINDOW_HANDLE_TYPE_X11, owner.nativeHandle()};
        }
        else if (native.type == VriWindowSystem_Wayland)
        {
            // NFD 1.3 has no xdg-foreign handle API. The portal accepts an unparented dialog.
            Logger::core().warn("The Wayland model picker opens without a parent window (NFD 1.3 limitation)");
        }
        else
        {
            throw std::runtime_error("Open model dialog: unsupported Linux window system");
        }
        NFD::UniquePath path;
        const auto      result = NFD::OpenDialog(path, &filter, 1, nullptr, parent);
        if (result == NFD_CANCEL)
        {
            return std::nullopt;
        }
        if (result != NFD_OKAY)
        {
            throw std::runtime_error(std::string("Open model dialog: ") + NFD::GetError());
        }
        return std::filesystem::path(path.get());
    }
} // namespace vultra
