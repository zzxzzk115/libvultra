#pragma once

// Central access point for builtin engine resources (shaders, fonts, textures, render
// graphs) addressed by stable logical paths under the conceptual `builtin://` scheme.
//
// The byte source is set ONCE at startup, before ShaderSystem/ImGuiSystem initialize:
//   - self-contained binaries (editor, examples) set it to the embedded builtin.vpk
//     (mounted from memory by the vultra_builtin_pack component);
//   - the export-template runtime sets it to its project VPK (export injected the
//     builtin resources there).
//
// Consumers (shader_system, imgui_system, texture loaders, render-graph registry) read
// through here and stay oblivious to where the bytes come from.

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace vfilesystem
{
    class IFileSystem;
} // namespace vfilesystem

namespace vultra::builtin
{
    // Install the backend that serves builtin resources. Logical paths passed to read()
    // are resolved against this backend (e.g. "shaders/builtin_highend.vshlib").
    void setSource(std::shared_ptr<vfilesystem::IFileSystem> backend);

    // True once a source has been installed.
    bool hasSource();

    // Read a builtin resource by logical path (no scheme prefix). Returns false if no
    // source is installed or the entry does not exist; `out` is untouched on failure.
    bool read(std::string_view logicalPath, std::vector<std::byte>& out);
} // namespace vultra::builtin

namespace vultra
{
    // Installs the embedded builtin.vpk as the builtin:: resource source. Defined per
    // self-contained binary by the `vultra.builtin_pack` xmake rule (it embeds the pack
    // and compiles the platform accessor). Call once at startup, before engine init.
    // Not available in the thin export-template runtime (which mounts a project VPK).
    void mountBuiltinPack();
} // namespace vultra
