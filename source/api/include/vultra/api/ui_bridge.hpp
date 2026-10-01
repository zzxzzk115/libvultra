#pragma once

#include <vultra/api/vultra_ui.generated.h>

namespace vultra
{
    class EditorGui;

    VultraUiFrame      makeUiFrame(EditorGui& gui);
    const VultraUiApi& uiApi();
} // namespace vultra
