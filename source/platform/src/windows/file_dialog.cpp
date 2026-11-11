#include <vultra/platform/os/file_dialog.hpp>

// clang-format off
#include <windows.h>
#include <commdlg.h>
// clang-format on

#include <array>

namespace vultra
{
    std::expected<std::optional<std::filesystem::path>, Error> openModelDialog(const Window& owner)
    {
        const auto handle = owner.nativeHandle();
        if (!handle)
        {
            return std::unexpected(handle.error());
        }
        std::array<wchar_t, 32768> file {};
        OPENFILENAMEW              info {};
        info.lStructSize = sizeof(info);
        info.hwndOwner   = static_cast<HWND>(*handle);
        info.lpstrFilter = L"Static models (*.gltf;*.glb;*.obj;*.fbx)\0*.gltf;*.glb;*.obj;*.fbx\0All files\0*.*\0";
        info.lpstrFile   = file.data();
        info.nMaxFile    = DWORD(file.size());
        info.lpstrTitle  = L"Open glTF model";
        info.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        if (GetOpenFileNameW(&info))
        {
            return std::filesystem::path(file.data());
        }
        if (const auto error = CommDlgExtendedError())
        {
            return makeError("Open model dialog failed: {}", error);
        }
        return std::nullopt;
    }
} // namespace vultra
