#include "editor_app/mcp_stdio_bridge.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// winsock2.h must precede windows.h, otherwise windows.h pulls in the legacy winsock.h and the
// two redefine each other.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#elif defined(__APPLE__)
#include <arpa/inet.h>
#include <cerrno>
#include <mach-o/dyld.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace vultra_app
{
    std::filesystem::path currentExecutablePath()
    {
#if defined(_WIN32)
        std::wstring buffer(1024, L'\0');
        const DWORD  size = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0)
            return {};
        buffer.resize(size);
        return std::filesystem::path {buffer};
#elif defined(__APPLE__)
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            return {};
        return std::filesystem::canonical(std::filesystem::path {buffer});
#else
        std::error_code ec;
        auto            path = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::filesystem::path {} : path;
#endif
    }

    namespace
    {
#if defined(_WIN32)
        using SocketHandle = SOCKET;
        constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
        void                   closeSocket(SocketHandle s) { if (s != kInvalidSocket) closesocket(s); }
#else
        using SocketHandle = int;
        constexpr SocketHandle kInvalidSocket = -1;
        void                   closeSocket(SocketHandle s) { if (s != kInvalidSocket) ::close(s); }
#endif

        // One JSON-RPC request -> one fresh connection (the server replies with Connection: close).
        // Returns the response body, or fills `error` and returns false.
        bool postToServer(const std::string& host,
                          std::uint16_t      port,
                          const std::string& body,
                          std::string&       responseBody,
                          std::string&       error)
        {
            in_addr address {};
            if (::inet_pton(AF_INET, host.c_str(), &address) != 1)
            {
                error = "invalid host: " + host;
                return false;
            }

            SocketHandle sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock == kInvalidSocket)
            {
                error = "failed to create socket";
                return false;
            }

            sockaddr_in addr {};
            addr.sin_family = AF_INET;
            addr.sin_addr   = address;
            addr.sin_port   = htons(port);
            if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
            {
                closeSocket(sock);
                error = "failed to connect to " + host + ":" + std::to_string(port);
                return false;
            }

            std::string request;
            request += "POST /mcp HTTP/1.1\r\n";
            request += "Host: " + host + ":" + std::to_string(port) + "\r\n";
            request += "Content-Type: application/json\r\n";
            request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
            request += "Connection: close\r\n\r\n";
            request += body;

            std::size_t sent = 0;
            while (sent < request.size())
            {
#if defined(_WIN32)
                const int n = ::send(sock, request.data() + sent, static_cast<int>(request.size() - sent), 0);
#else
                const ::ssize_t n = ::send(sock, request.data() + sent, request.size() - sent, 0);
#endif
                if (n <= 0)
                {
                    closeSocket(sock);
                    error = "failed to send request";
                    return false;
                }
                sent += static_cast<std::size_t>(n);
            }

            std::string          raw;
            std::array<char, 4096> buffer {};
            while (true)
            {
#if defined(_WIN32)
                const int received = ::recv(sock, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
                const ::ssize_t received = ::recv(sock, buffer.data(), buffer.size(), 0);
#endif
                if (received < 0)
                {
                    closeSocket(sock);
                    error = "failed to read response";
                    return false;
                }
                if (received == 0)
                    break; // server closed the connection
                raw.append(buffer.data(), static_cast<std::size_t>(received));
            }
            closeSocket(sock);

            const auto headerEnd = raw.find("\r\n\r\n");
            if (headerEnd == std::string::npos)
            {
                error = "malformed HTTP response";
                return false;
            }
            responseBody = raw.substr(headerEnd + 4);
            return true;
        }

        std::string jsonRpcError(const nlohmann::json& id, int code, const std::string& message)
        {
            const nlohmann::json error = {
                {"jsonrpc", "2.0"},
                {"id", id.is_null() ? nlohmann::json(nullptr) : id},
                {"error", {{"code", code}, {"message", message}}},
            };
            return error.dump();
        }
    } // namespace

    int runMcpStdioBridge(const std::string& host, std::uint16_t port)
    {
#if defined(_WIN32)
        WSADATA wsaData {};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            return 1;
        // Avoid CRLF translation / UTF-8 corruption on the JSON byte stream.
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif

        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            // Determine whether this is a request (has "id") or a notification (no reply expected).
            nlohmann::json id            = nullptr;
            bool           isNotification = false;
            try
            {
                const auto parsed = nlohmann::json::parse(line);
                if (parsed.is_object() && parsed.contains("id"))
                    id = parsed["id"];
                else
                    isNotification = true;
            }
            catch (const std::exception&)
            {
                // Unparseable: forward as-is and echo whatever the server says.
            }

            std::string responseBody;
            std::string error;
            const bool  ok = postToServer(host, port, line, responseBody, error);

            if (isNotification)
                continue; // notifications get no response on stdio

            if (ok)
            {
                std::cout << responseBody << '\n';
            }
            else
            {
                std::cout << jsonRpcError(id, -32000, "runtime MCP bridge: " + error) << '\n';
            }
            std::cout.flush();
        }

#if defined(_WIN32)
        WSACleanup();
#endif
        return 0;
    }
} // namespace vultra_app
