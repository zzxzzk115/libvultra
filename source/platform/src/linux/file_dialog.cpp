#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/file_dialog.hpp>
#include <vultra/platform/window.hpp>

#include <nfd.hpp>

namespace vultra
{
    std::expected<std::optional<std::filesystem::path>, Error> openModelDialog(const Window& owner)
    {
        nfdwindowhandle_t parent {};
        const auto        native = platform::nativeWindow(owner);
        if (native.type == VriWindowSystem_Xlib)
        {
            const auto handle = owner.nativeHandle();
            if (!handle)
            {
                return std::unexpected(handle.error());
            }
            parent = {NFD_WINDOW_HANDLE_TYPE_X11, *handle};
        }
        else if (native.type == VriWindowSystem_Wayland)
        {
            // NFD 1.3 has no xdg-foreign handle API. The portal accepts an unparented dialog.
            Logger::core().warn("The Wayland model picker opens without a parent window (NFD 1.3 limitation)");
        }
        else
        {
            return makeError("Open model dialog: unsupported Linux window system");
        }
        // Built without NFD_THROWS_EXCEPTIONS: a failed NFD_Init surfaces through OpenDialog and GetError.
        const NFD::Guard      guard;
        const nfdfilteritem_t filter {"Static models", "gltf,glb,obj,fbx"};
        NFD::UniquePath       path;
        const auto            result = NFD::OpenDialog(path, &filter, 1, nullptr, parent);
        if (result == NFD_CANCEL)
        {
            return std::nullopt;
        }
        if (result != NFD_OKAY)
        {
            return makeError("Open model dialog: {}", NFD::GetError());
        }
        return std::filesystem::path(path.get());
    }
} // namespace vultra
