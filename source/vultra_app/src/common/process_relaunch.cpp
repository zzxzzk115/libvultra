#include "common/process_relaunch.hpp"

#include <cstddef>
#include <string>
#include <vector>

#if defined(_WIN32)
#    include <windows.h>
#    include <shellapi.h>
#    pragma comment(lib, "Shell32.lib")
#endif

namespace vultra_app
{
#if defined(_WIN32)
    namespace
    {
        std::wstring quoteCommandLineArg(const std::wstring& arg)
        {
            if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
                return arg;
            std::wstring out         = L"\"";
            std::size_t  backslashes = 0;
            for (const wchar_t ch : arg)
            {
                if (ch == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (ch == L'"')
                {
                    out.append(backslashes * 2 + 1, L'\\');
                    out.push_back(ch);
                    backslashes = 0;
                    continue;
                }
                out.append(backslashes, L'\\');
                backslashes = 0;
                out.push_back(ch);
            }
            out.append(backslashes * 2, L'\\');
            out.push_back(L'"');
            return out;
        }
    } // namespace

    bool relaunchIntoProject(const std::filesystem::path& projectDir)
    {
        int       argc = 0;
        wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (argv == nullptr || argc < 1)
            return false;

        // Carry over the original options, but force --editor/--project to the requested project.
        // (A launcher-started session has no --project; replaying its command line verbatim would
        // boot back into the launcher and miss the pre-render-device plugin load window.)
        std::wstring command = L"cmd.exe /c timeout /t 2 /nobreak >nul & start \"\" ";
        command += quoteCommandLineArg(argv[0]);
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring arg = argv[i];
            if (arg == L"--editor" || arg.starts_with(L"--project="))
                continue;
            if (arg == L"--project")
            {
                ++i; // skip its value too
                continue;
            }
            command += L' ';
            command += quoteCommandLineArg(arg);
        }
        ::LocalFree(argv);
        command += L" --editor --project ";
        command += quoteCommandLineArg(projectDir.wstring());

        STARTUPINFOW        startup {};
        PROCESS_INFORMATION process {};
        startup.cb = sizeof(startup);
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');
        if (!::CreateProcessW(nullptr,
                              mutableCommand.data(),
                              nullptr,
                              nullptr,
                              FALSE,
                              CREATE_NO_WINDOW | DETACHED_PROCESS,
                              nullptr,
                              nullptr,
                              &startup,
                              &process))
            return false;
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
        return true;
    }
#else
    bool relaunchIntoProject(const std::filesystem::path&) { return false; }
#endif
} // namespace vultra_app
