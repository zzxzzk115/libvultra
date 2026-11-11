#include <vultra/platform/os/file_dialog.hpp>

#include <nfd.hpp>

namespace vultra
{
    std::expected<std::optional<std::filesystem::path>, Error> openModelDialog(const Window& owner)
    {
        const auto handle = owner.nativeHandle();
        if (!handle)
        {
            return std::unexpected(handle.error());
        }
        // Built without NFD_THROWS_EXCEPTIONS: a failed NFD_Init surfaces through OpenDialog and GetError.
        const NFD::Guard        guard;
        const nfdfilteritem_t   filter {"Static models", "gltf,glb,obj,fbx"};
        const nfdwindowhandle_t parent {NFD_WINDOW_HANDLE_TYPE_COCOA, *handle};
        NFD::UniquePath         path;
        const auto              result = NFD::OpenDialog(path, &filter, 1, nullptr, parent);
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
