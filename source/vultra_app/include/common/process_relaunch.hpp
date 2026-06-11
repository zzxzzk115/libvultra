#pragma once

#include <filesystem>

namespace vultra_app
{
    // Relaunch this process into the given project: spawns a detached helper that waits for this
    // process to shut down, then re-runs the executable with the original command-line options
    // carried over -- except --editor/--project, which are forced to the given project. The caller
    // closes the window afterwards.
    //
    // This is how anything that only applies at process startup gets applied: pre-render-device
    // plugins (e.g. Vulkan-hooking DLSS) must be loaded before the render device exists, so neither
    // an in-process launcher->editor transition nor a mid-session enable can activate them.
    [[nodiscard]] bool relaunchIntoProject(const std::filesystem::path& projectDir);
} // namespace vultra_app
