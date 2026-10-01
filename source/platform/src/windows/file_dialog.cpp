#include <vultra/platform/os/file_dialog.hpp>

// clang-format off
#include <windows.h>
#include <commdlg.h>
// clang-format on

#include <array>
#include <stdexcept>
#include <string>

namespace vultra
{
    std::optional<std::filesystem::path> openModelDialog(const Window& owner)
    {
        std::array<wchar_t, 32768> file {};
        OPENFILENAMEW              info {};
        info.lStructSize = sizeof(info);
        info.hwndOwner   = static_cast<HWND>(owner.nativeHandle());
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
            throw std::runtime_error("Open model dialog failed: " + std::to_string(error));
        }
        return std::nullopt;
    }
} // namespace vultra
