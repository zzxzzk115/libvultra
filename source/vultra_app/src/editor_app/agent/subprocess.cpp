#include "editor_app/agent/subprocess.hpp"

#include <mutex>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace vultra_app::agent
{
    namespace
    {
        // Pulls the first '\n'-terminated line out of `buffer`, erasing it (and the newline)
        // from the buffer. A trailing '\r' is stripped so Windows CRLF output is normalised.
        std::optional<std::string> popLine(std::string& buffer)
        {
            const auto pos = buffer.find('\n');
            if (pos == std::string::npos)
                return std::nullopt;
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return line;
        }
    } // namespace

#if defined(_WIN32)

    namespace
    {
        // Quote a single argument per the CommandLineToArgvW rules so paths with spaces and
        // embedded quotes survive the round-trip through CreateProcessW's single string.
        std::wstring quoteArg(const std::wstring& arg)
        {
            if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
                return arg;

            std::wstring out;
            out.push_back(L'"');
            for (auto it = arg.begin();; ++it)
            {
                std::size_t backslashes = 0;
                while (it != arg.end() && *it == L'\\')
                {
                    ++it;
                    ++backslashes;
                }
                if (it == arg.end())
                {
                    out.append(backslashes * 2, L'\\');
                    break;
                }
                if (*it == L'"')
                {
                    out.append(backslashes * 2 + 1, L'\\');
                    out.push_back(L'"');
                }
                else
                {
                    out.append(backslashes, L'\\');
                    out.push_back(*it);
                }
            }
            out.push_back(L'"');
            return out;
        }

        std::wstring widen(std::string_view utf8)
        {
            if (utf8.empty())
                return {};
            const int len =
                ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            std::wstring wide(static_cast<std::size_t>(len), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
            return wide;
        }
    } // namespace

    struct Subprocess::Impl
    {
        PROCESS_INFORMATION processInfo {};
        HANDLE              stdinWrite {nullptr};
        HANDLE              stdoutRead {nullptr};
        std::mutex          writeMutex;
        std::string         readBuffer;
        bool                stdinClosed {false};
        bool                reachedEof {false};
        std::optional<int>  cachedExitCode;
    };

    bool Subprocess::start(const SubprocessOptions& options, std::string* error)
    {
        auto fail = [&](const std::string& message) {
            if (error)
                *error = message;
            return false;
        };

        m_Impl = std::make_unique<Impl>();

        SECURITY_ATTRIBUTES sa {};
        sa.nLength              = sizeof(sa);
        sa.bInheritHandle       = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE childStdinRead   = nullptr;
        HANDLE childStdoutWrite = nullptr;
        HANDLE nulHandle        = nullptr;

        auto cleanupPartial = [&]() {
            if (childStdinRead)
                ::CloseHandle(childStdinRead);
            if (childStdoutWrite)
                ::CloseHandle(childStdoutWrite);
            if (nulHandle)
                ::CloseHandle(nulHandle);
            if (m_Impl->stdinWrite)
                ::CloseHandle(m_Impl->stdinWrite);
            if (m_Impl->stdoutRead)
                ::CloseHandle(m_Impl->stdoutRead);
            m_Impl.reset();
        };

        if (!::CreatePipe(&childStdinRead, &m_Impl->stdinWrite, &sa, 0))
            return fail("CreatePipe(stdin) failed");
        // Our write end must not be inherited by the child, else it never sees stdin EOF.
        ::SetHandleInformation(m_Impl->stdinWrite, HANDLE_FLAG_INHERIT, 0);

        if (!::CreatePipe(&m_Impl->stdoutRead, &childStdoutWrite, &sa, 0))
        {
            cleanupPartial();
            return fail("CreatePipe(stdout) failed");
        }
        ::SetHandleInformation(m_Impl->stdoutRead, HANDLE_FLAG_INHERIT, 0);

        // Child stderr: merged into stdout when requested, otherwise discarded to NUL so the
        // CLI's diagnostics never pollute the structured stdout stream we parse.
        HANDLE childStderr = childStdoutWrite;
        if (!options.captureStderr)
        {
            nulHandle = ::CreateFileW(L"NUL",
                                      GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      &sa,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
            if (nulHandle != INVALID_HANDLE_VALUE)
                childStderr = nulHandle;
            else
                nulHandle = nullptr;
        }

        std::wstring commandLine = quoteArg(widen(options.executable));
        for (const auto& arg : options.args)
        {
            commandLine.push_back(L' ');
            commandLine.append(quoteArg(widen(arg)));
        }

        STARTUPINFOW si {};
        si.cb         = sizeof(si);
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdInput  = childStdinRead;
        si.hStdOutput = childStdoutWrite;
        si.hStdError  = childStderr;

        std::wstring workingDirStorage;
        const wchar_t* workingDir = nullptr;
        if (options.workingDir.has_value())
        {
            workingDirStorage = options.workingDir->wstring();
            workingDir        = workingDirStorage.c_str();
        }

        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');

        const BOOL ok = ::CreateProcessW(nullptr,
                                         mutableCommandLine.data(),
                                         nullptr,
                                         nullptr,
                                         TRUE,
                                         CREATE_NO_WINDOW,
                                         nullptr,
                                         workingDir,
                                         &si,
                                         &m_Impl->processInfo);
        if (!ok)
        {
            const DWORD code = ::GetLastError();
            cleanupPartial();
            return fail("CreateProcessW failed (error " + std::to_string(code) + ")");
        }

        // The child now owns its ends; the parent must close its copies or readers never see EOF.
        ::CloseHandle(childStdinRead);
        ::CloseHandle(childStdoutWrite);
        if (nulHandle)
            ::CloseHandle(nulHandle);
        return true;
    }

    bool Subprocess::writeLine(std::string_view line)
    {
        if (!m_Impl)
            return false;
        std::lock_guard lock {m_Impl->writeMutex};
        if (m_Impl->stdinClosed || !m_Impl->stdinWrite)
            return false;

        std::string payload(line);
        payload.push_back('\n');
        std::size_t written = 0;
        while (written < payload.size())
        {
            DWORD chunk = 0;
            if (!::WriteFile(m_Impl->stdinWrite,
                             payload.data() + written,
                             static_cast<DWORD>(payload.size() - written),
                             &chunk,
                             nullptr))
                return false;
            written += chunk;
        }
        return true;
    }

    std::optional<std::string> Subprocess::readLine(std::chrono::milliseconds timeout)
    {
        if (!m_Impl)
            return std::nullopt;

        if (auto line = popLine(m_Impl->readBuffer))
            return line;

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        char       chunk[4096];
        while (true)
        {
            DWORD available = 0;
            if (!::PeekNamedPipe(m_Impl->stdoutRead, nullptr, 0, nullptr, &available, nullptr))
            {
                m_Impl->reachedEof = true; // pipe broken => child closed stdout
                break;
            }
            if (available > 0)
            {
                DWORD read = 0;
                const DWORD want = available < sizeof(chunk) ? available : static_cast<DWORD>(sizeof(chunk));
                if (!::ReadFile(m_Impl->stdoutRead, chunk, want, &read, nullptr) || read == 0)
                {
                    m_Impl->reachedEof = true;
                    break;
                }
                m_Impl->readBuffer.append(chunk, read);
                if (auto line = popLine(m_Impl->readBuffer))
                    return line;
                continue; // drained what was ready; loop to check for more without sleeping
            }

            if (std::chrono::steady_clock::now() >= deadline)
                return std::nullopt;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // EOF: hand back any trailing unterminated content once.
        if (!m_Impl->readBuffer.empty())
        {
            std::string remainder = std::move(m_Impl->readBuffer);
            m_Impl->readBuffer.clear();
            if (!remainder.empty() && remainder.back() == '\r')
                remainder.pop_back();
            return remainder;
        }
        return std::nullopt;
    }

    void Subprocess::closeStdin()
    {
        if (!m_Impl)
            return;
        std::lock_guard lock {m_Impl->writeMutex};
        if (m_Impl->stdinWrite)
        {
            ::CloseHandle(m_Impl->stdinWrite);
            m_Impl->stdinWrite = nullptr;
        }
        m_Impl->stdinClosed = true;
    }

    bool Subprocess::eof() const { return m_Impl ? m_Impl->reachedEof : true; }

    bool Subprocess::isRunning() const
    {
        if (!m_Impl || m_Impl->cachedExitCode.has_value())
            return false;
        DWORD code = 0;
        if (!::GetExitCodeProcess(m_Impl->processInfo.hProcess, &code))
            return false;
        return code == STILL_ACTIVE;
    }

    std::optional<int> Subprocess::exitCode() const
    {
        if (!m_Impl)
            return std::nullopt;
        if (m_Impl->cachedExitCode.has_value())
            return m_Impl->cachedExitCode;
        DWORD code = 0;
        if (::GetExitCodeProcess(m_Impl->processInfo.hProcess, &code) && code != STILL_ACTIVE)
        {
            m_Impl->cachedExitCode = static_cast<int>(code);
            return m_Impl->cachedExitCode;
        }
        return std::nullopt;
    }

    void Subprocess::terminate()
    {
        if (!m_Impl)
            return;
        closeStdin();
        if (isRunning())
            ::TerminateProcess(m_Impl->processInfo.hProcess, 1);
    }

    std::optional<int> Subprocess::waitFor(std::chrono::milliseconds timeout)
    {
        if (!m_Impl)
            return std::nullopt;
        if (auto code = exitCode())
            return code;
        const DWORD waitMs =
            timeout.count() < 0 ? INFINITE : static_cast<DWORD>(timeout.count());
        if (::WaitForSingleObject(m_Impl->processInfo.hProcess, waitMs) == WAIT_OBJECT_0)
            return exitCode();
        return std::nullopt;
    }

    Subprocess::Subprocess() = default;

    Subprocess::~Subprocess()
    {
        if (!m_Impl)
            return;
        terminate();
        if (m_Impl->stdoutRead)
            ::CloseHandle(m_Impl->stdoutRead);
        if (m_Impl->processInfo.hThread)
            ::CloseHandle(m_Impl->processInfo.hThread);
        if (m_Impl->processInfo.hProcess)
            ::CloseHandle(m_Impl->processInfo.hProcess);
    }

#else // ---------------------------------------------------------------- POSIX

    struct Subprocess::Impl
    {
        ::pid_t            pid {-1};
        int                stdinWrite {-1};
        int                stdoutRead {-1};
        std::mutex         writeMutex;
        std::string        readBuffer;
        bool               stdinClosed {false};
        bool               reachedEof {false};
        std::optional<int> cachedExitCode;
    };

    bool Subprocess::start(const SubprocessOptions& options, std::string* error)
    {
        auto fail = [&](const std::string& message) {
            if (error)
                *error = message;
            return false;
        };

        m_Impl = std::make_unique<Impl>();

        int inPipe[2]  = {-1, -1}; // child stdin:  parent writes inPipe[1], child reads inPipe[0]
        int outPipe[2] = {-1, -1}; // child stdout: child writes outPipe[1], parent reads outPipe[0]
        if (::pipe(inPipe) != 0)
        {
            m_Impl.reset();
            return fail("pipe(stdin) failed");
        }
        if (::pipe(outPipe) != 0)
        {
            ::close(inPipe[0]);
            ::close(inPipe[1]);
            m_Impl.reset();
            return fail("pipe(stdout) failed");
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, inPipe[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO);
        if (options.captureStderr)
            posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDERR_FILENO);
        // Close the parent-side ends in the child.
        posix_spawn_file_actions_addclose(&actions, inPipe[1]);
        posix_spawn_file_actions_addclose(&actions, outPipe[0]);
        posix_spawn_file_actions_addclose(&actions, inPipe[0]);
        posix_spawn_file_actions_addclose(&actions, outPipe[1]);
#if defined(__GLIBC__) || defined(__APPLE__)
        if (options.workingDir.has_value())
            posix_spawn_file_actions_addchdir_np(&actions, options.workingDir->c_str());
#endif

        std::vector<std::string> storage;
        storage.reserve(options.args.size() + 1);
        storage.push_back(options.executable);
        for (const auto& arg : options.args)
            storage.push_back(arg);

        std::vector<char*> argv;
        argv.reserve(storage.size() + 1);
        for (auto& s : storage)
            argv.push_back(s.data());
        argv.push_back(nullptr);

        const int rc = ::posix_spawnp(&m_Impl->pid, options.executable.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);

        // Parent no longer needs the child-side ends regardless of success.
        ::close(inPipe[0]);
        ::close(outPipe[1]);

        if (rc != 0)
        {
            ::close(inPipe[1]);
            ::close(outPipe[0]);
            m_Impl.reset();
            return fail(std::string("posix_spawnp failed: ") + std::strerror(rc));
        }

        m_Impl->stdinWrite = inPipe[1];
        m_Impl->stdoutRead = outPipe[0];
        return true;
    }

    bool Subprocess::writeLine(std::string_view line)
    {
        if (!m_Impl)
            return false;
        std::lock_guard lock {m_Impl->writeMutex};
        if (m_Impl->stdinClosed || m_Impl->stdinWrite < 0)
            return false;

        std::string payload(line);
        payload.push_back('\n');
        std::size_t written = 0;
        while (written < payload.size())
        {
            const ::ssize_t n = ::write(m_Impl->stdinWrite, payload.data() + written, payload.size() - written);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            written += static_cast<std::size_t>(n);
        }
        return true;
    }

    std::optional<std::string> Subprocess::readLine(std::chrono::milliseconds timeout)
    {
        if (!m_Impl)
            return std::nullopt;

        if (auto line = popLine(m_Impl->readBuffer))
            return line;

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        char       chunk[4096];
        while (true)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
                return std::nullopt;
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

            ::pollfd pfd {};
            pfd.fd     = m_Impl->stdoutRead;
            pfd.events = POLLIN;
            const int pr = ::poll(&pfd, 1, static_cast<int>(remaining));
            if (pr < 0)
            {
                if (errno == EINTR)
                    continue;
                m_Impl->reachedEof = true;
                break;
            }
            if (pr == 0)
                return std::nullopt; // timed out

            const ::ssize_t n = ::read(m_Impl->stdoutRead, chunk, sizeof(chunk));
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                m_Impl->reachedEof = true;
                break;
            }
            if (n == 0)
            {
                m_Impl->reachedEof = true; // EOF
                break;
            }
            m_Impl->readBuffer.append(chunk, static_cast<std::size_t>(n));
            if (auto line = popLine(m_Impl->readBuffer))
                return line;
        }

        if (!m_Impl->readBuffer.empty())
        {
            std::string remainder = std::move(m_Impl->readBuffer);
            m_Impl->readBuffer.clear();
            if (!remainder.empty() && remainder.back() == '\r')
                remainder.pop_back();
            return remainder;
        }
        return std::nullopt;
    }

    void Subprocess::closeStdin()
    {
        if (!m_Impl)
            return;
        std::lock_guard lock {m_Impl->writeMutex};
        if (m_Impl->stdinWrite >= 0)
        {
            ::close(m_Impl->stdinWrite);
            m_Impl->stdinWrite = -1;
        }
        m_Impl->stdinClosed = true;
    }

    bool Subprocess::eof() const { return m_Impl ? m_Impl->reachedEof : true; }

    bool Subprocess::isRunning() const
    {
        if (!m_Impl || m_Impl->cachedExitCode.has_value() || m_Impl->pid < 0)
            return false;
        int        status = 0;
        const auto r      = ::waitpid(m_Impl->pid, &status, WNOHANG);
        if (r == 0)
            return true; // still running
        if (r == m_Impl->pid)
        {
            if (WIFEXITED(status))
                m_Impl->cachedExitCode = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                m_Impl->cachedExitCode = 128 + WTERMSIG(status);
            else
                m_Impl->cachedExitCode = -1;
        }
        return false;
    }

    std::optional<int> Subprocess::exitCode() const
    {
        if (!m_Impl)
            return std::nullopt;
        if (m_Impl->cachedExitCode.has_value())
            return m_Impl->cachedExitCode;
        // isRunning() refreshes the cached code as a side effect via waitpid.
        (void)isRunning();
        return m_Impl->cachedExitCode;
    }

    void Subprocess::terminate()
    {
        if (!m_Impl)
            return;
        closeStdin();
        if (m_Impl->pid > 0 && isRunning())
        {
            ::kill(m_Impl->pid, SIGTERM);
            // Give it a brief grace period, then force it.
            for (int i = 0; i < 20 && isRunning(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (isRunning())
                ::kill(m_Impl->pid, SIGKILL);
        }
    }

    std::optional<int> Subprocess::waitFor(std::chrono::milliseconds timeout)
    {
        if (!m_Impl)
            return std::nullopt;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true)
        {
            if (auto code = exitCode())
                return code;
            if (timeout.count() >= 0 && std::chrono::steady_clock::now() >= deadline)
                return std::nullopt;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    Subprocess::Subprocess() = default;

    Subprocess::~Subprocess()
    {
        if (!m_Impl)
            return;
        terminate();
        if (m_Impl->stdoutRead >= 0)
            ::close(m_Impl->stdoutRead);
        // Reap the child so it does not linger as a zombie.
        if (m_Impl->pid > 0 && !m_Impl->cachedExitCode.has_value())
        {
            int status = 0;
            ::waitpid(m_Impl->pid, &status, 0);
        }
    }

#endif
} // namespace vultra_app::agent
