#pragma once

#include <vultra/function/services/asset_service.hpp> // vultra::AssetDiagnostic

#include <vector>

namespace vultra_app
{
    struct EditorContext;

    // Supplies diagnostics for one family of source files the code editor can open
    // (imported assets, render passes, ...). The editor owns a set of providers and
    // aggregates their diagnostics for the currently open file, so support for a new
    // source format is added by implementing this interface rather than by editing
    // the editor's collection logic.
    class ISourceDiagnosticProvider
    {
    public:
        virtual ~ISourceDiagnosticProvider() = default;

        // Short identifier, for logging/debugging (e.g. "import", "render-pass").
        [[nodiscard]] virtual const char* name() const = 0;

        // Every diagnostic this provider currently knows about (each carries its own
        // source path); the editor filters them down to the open file.
        [[nodiscard]] virtual std::vector<vultra::AssetDiagnostic> collect(EditorContext& ctx) const = 0;
    };
} // namespace vultra_app
