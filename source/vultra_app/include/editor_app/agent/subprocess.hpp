#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vultra_app::agent
{
    struct SubprocessOptions
    {
        std::string                          executable; // resolved via PATH if not absolute
        std::vector<std::string>             args;       // does NOT include argv[0]
        std::optional<std::filesystem::path> workingDir;
        // When true the child's stderr is merged into the stdout pipe. Leave false for
        // tools (like claude) that emit structured data on stdout and diagnostics on stderr,
        // so the parser only ever sees the structured stream.
        bool captureStderr {false};
    };

    // Cross-platform bidirectional child process: write to its stdin and read its stdout
    // line-by-line, concurrently, from another thread.
    //
    // Why this exists instead of reusing openPipeWrite()/_popen in runtime_mcp_editor_tools.cpp:
    // _popen() yields a single one-directional FILE* (write-only here, feeding ffmpeg frames)
    // and cannot read the child's stdout. Driving an agent CLI (claude) in stream-json mode
    // needs simultaneous write+read, which requires real OS pipes + CreateProcess/posix_spawn.
    //
    // readLine() is timeout-bounded so a reader loop can observe a stop request even when the
    // child produces no output. All public methods are safe to call from one writer thread and
    // one reader thread concurrently (writeLine vs readLine touch independent handles/state).
    class Subprocess
    {
    public:
        // Constructor and destructor are out-of-line (defined in the .cpp where Impl is complete)
        // so the unique_ptr<Impl> pimpl deleter never instantiates against the incomplete type.
        Subprocess();
        ~Subprocess();

        Subprocess(const Subprocess&)            = delete;
        Subprocess& operator=(const Subprocess&) = delete;
        Subprocess(Subprocess&&)                 = delete;
        Subprocess& operator=(Subprocess&&)      = delete;

        // Spawns the child. On failure returns false and fills *error (if provided).
        bool start(const SubprocessOptions& options, std::string* error = nullptr);

        // Appends '\n' and flushes to the child's stdin. Returns false if stdin is closed
        // or the write failed. Thread-safe against readLine().
        bool writeLine(std::string_view line);

        // Reads one '\n'-terminated line from the child's stdout, blocking up to `timeout`.
        // Returns the line WITHOUT the trailing newline (a trailing '\r' is also stripped).
        // Returns std::nullopt on timeout or on EOF-with-no-buffered-data; use eof() to tell
        // them apart.
        std::optional<std::string> readLine(std::chrono::milliseconds timeout);

        // Sends EOF to the child by closing our stdin write handle.
        void closeStdin();

        [[nodiscard]] bool                eof() const;       // stdout reached end of stream
        [[nodiscard]] bool                isRunning() const; // child process still alive
        [[nodiscard]] std::optional<int>  exitCode() const;  // set once the child has exited

        // Closes stdin, then forcibly terminates the child if still running. Idempotent.
        void terminate();

        // Waits up to `timeout` for the child to exit. Returns the exit code, or std::nullopt
        // on timeout.
        std::optional<int> waitFor(std::chrono::milliseconds timeout);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra_app::agent
