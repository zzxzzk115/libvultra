#include "editor_app/runtime_mcp_server.hpp"
#include "editor_app/runtime_mcp_server_internal.hpp"

#include "app_state.hpp"
#include "editor_app/editor_app.hpp"
#include "editor_app/vultra_package.hpp"
#include "vproject.hpp"

#include <vultra/core/rhi/structs/pixel_format.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/rendering/runtime_profiler.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/frame_debugger_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/world.hpp>
#include <vultra/function/world/components/box_shape_component.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/rigid_body_component.hpp>
#include <vultra/function/world/components/sphere_shape_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>

#include <vasset/vasset_type.hpp>

#include <vbase/core/scoped_enum_flags.hpp>

#include <nlohmann/json.hpp>
#include <stb_image_write.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace vultra_app
{
    namespace
    {
        constexpr std::string_view kProtocolVersion {"2025-03-26"};

#if defined(_WIN32)
        using SocketHandle = SOCKET;
        constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

        void closeSocket(const SocketHandle socket)
        {
            if (socket != kInvalidSocket)
                closesocket(socket);
        }

        std::string socketErrorText()
        {
            return "winsock error " + std::to_string(WSAGetLastError());
        }
#else
        using SocketHandle = int;
        constexpr SocketHandle kInvalidSocket = -1;

        void closeSocket(const SocketHandle socket)
        {
            if (socket != kInvalidSocket)
                close(socket);
        }

        std::string socketErrorText()
        {
            return std::strerror(errno);
        }
#endif

        struct SocketRuntime
        {
            SocketRuntime()
            {
#if defined(_WIN32)
                WSADATA data {};
                ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
            }

            ~SocketRuntime()
            {
#if defined(_WIN32)
                if (ok)
                    WSACleanup();
#endif
            }

#if defined(_WIN32)
            bool ok {false};
#else
            bool ok {true};
#endif
        };

        std::string lowerAscii(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
        }

        std::optional<size_t> contentLength(std::string_view headers)
        {
            std::string text {headers};
            std::istringstream stream {text};
            std::string line;
            while (std::getline(stream, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const auto colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
                auto key = lowerAscii(line.substr(0, colon));
                if (key != "content-length")
                    continue;
                auto value = line.substr(colon + 1);
                value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](const unsigned char ch) {
                    return !std::isspace(ch);
                }));
                try
                {
                    return static_cast<size_t>(std::stoull(value));
                }
                catch (...)
                {
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        std::string httpResponse(const int status, std::string_view reason, const std::string& body)
        {
            std::ostringstream out;
            out << "HTTP/1.1 " << status << ' ' << reason << "\r\n";
            out << "Content-Type: application/json\r\n";
            out << "Content-Length: " << body.size() << "\r\n";
            out << "Connection: close\r\n\r\n";
            out << body;
            return out.str();
        }

        nlohmann::json jsonRpcError(const nlohmann::json& id, const int code, std::string message)
        {
            return {
                {"jsonrpc", "2.0"},
                {"id", id.is_null() ? nlohmann::json(nullptr) : id},
                {"error", {{"code", code}, {"message", std::move(message)}}},
            };
        }

        nlohmann::json jsonRpcResult(const nlohmann::json& id, nlohmann::json result)
        {
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
        }

        nlohmann::json toolJson(nlohmann::json payload)
        {
            return {
                {"content",
                 nlohmann::json::array({{{"type", "text"}, {"text", payload.dump(2)}}})},
            };
        }

        nlohmann::json toolError(std::string message)
        {
            return {
                {"isError", true},
                {"content",
                 nlohmann::json::array({{{"type", "text"},
                                          {"text", nlohmann::json({{"ok", false}, {"error", std::move(message)}}).dump(2)}}})},
            };
        }


        bool sendAll(const SocketHandle socket, std::string_view data)
        {
            while (!data.empty())
            {
#if defined(_WIN32)
                const int sent = send(socket, data.data(), static_cast<int>(data.size()), 0);
#else
                const ssize_t sent = send(socket, data.data(), data.size(), 0);
#endif
                if (sent <= 0)
                    return false;
                data.remove_prefix(static_cast<size_t>(sent));
            }
            return true;
        }

        std::string requestTarget(std::string_view requestText)
        {
            const auto firstLineEnd = requestText.find("\r\n");
            const auto firstLine = requestText.substr(0, firstLineEnd);
            const auto firstSpace = firstLine.find(' ');
            if (firstSpace == std::string_view::npos)
                return {};
            const auto secondSpace = firstLine.find(' ', firstSpace + 1);
            if (secondSpace == std::string_view::npos || secondSpace <= firstSpace + 1)
                return {};
            return std::string(firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1));
        }

        bool isGetRequest(std::string_view requestText)
        {
            return requestText.starts_with("GET ");
        }

        std::string httpTextResponse(const int status,
                                     std::string_view reason,
                                     std::string_view contentType,
                                     const std::string& body)
        {
            std::ostringstream out;
            out << "HTTP/1.1 " << status << ' ' << reason << "\r\n";
            out << "Content-Type: " << contentType << "\r\n";
            out << "Content-Length: " << body.size() << "\r\n";
            out << "Connection: close\r\n\r\n";
            out << body;
            return out.str();
        }
    } // namespace

    RuntimeMcpServer::~RuntimeMcpServer()
    {
        stop();
    }

    bool RuntimeMcpServer::start(std::string host, const uint16_t port, std::string* error)
    {
        std::unique_lock lock {m_Mutex};
        if (m_Running && m_Listening && m_Host == host && m_Port == port)
            return true;
        if (m_Running || m_ServerThread.joinable())
        {
            if (error)
                *error = "runtime MCP server is already running on " + endpoint();
            return false;
        }

        m_Host          = std::move(host);
        m_RequestedPort = port;
        m_Port          = port;
        m_StopRequested = false;
        m_Listening     = false;
        m_LastError.clear();
        m_Running      = true;
        m_ServerThread = std::thread([this] { serverLoop(); });
        m_StateCv.wait_for(lock, std::chrono::milliseconds(500), [this] {
            return m_Listening || !m_Running || !m_LastError.empty();
        });
        const bool started = m_Listening;
        if (!started && error)
            *error = m_LastError.empty() ? "runtime MCP server did not start listening" : m_LastError;
        lock.unlock();
        if (!started && m_ServerThread.joinable())
            m_ServerThread.join();
        return started;
    }

    void RuntimeMcpServer::stop()
    {
        if (m_RecordingWriterThread.joinable() || m_RecordingPipe)
            (void)stopRecordingWriter();
        m_Recording.active = false;
        stopVideoStreamClients();
        {
            std::lock_guard lock {m_Mutex};
            if (!m_Running && !m_ServerThread.joinable())
                return;
            m_StopRequested = true;
        }
        if (m_ServerThread.joinable())
            m_ServerThread.join();
        std::lock_guard lock {m_Mutex};
        m_Running = false;
        m_Listening = false;
        while (!m_PendingCalls.empty())
            m_PendingCalls.pop();
        m_DeferredCalls.clear();
    }

    bool RuntimeMcpServer::isRunning() const
    {
        std::lock_guard lock {m_Mutex};
        return m_Running && m_Listening;
    }

    bool RuntimeMcpServer::matchesDesiredEndpoint(const std::string& host, const uint16_t port) const
    {
        std::lock_guard lock {m_Mutex};
        return m_Running && m_Listening && m_Host == host && m_RequestedPort == port;
    }

    std::string RuntimeMcpServer::endpoint() const
    {
        return m_Host + ":" + std::to_string(m_Port);
    }

    std::string RuntimeMcpServer::lastError() const
    {
        std::lock_guard lock {m_Mutex};
        return m_LastError;
    }

    void RuntimeMcpServer::syncDesiredState(EditorContext& ctx)
    {
        const auto& settings = ctx.state.editorSettings;
        const bool  desired   = settings.enableAgent && settings.autoStartMcp;
        if (!desired)
        {
            stop();
            return;
        }

        std::string host = settings.mcpHost.empty() ? "127.0.0.1" : settings.mcpHost;
        if (host != "127.0.0.1" && host != "localhost")
        {
            host = "127.0.0.1";
            ctx.state.statusMessage = "Runtime MCP only supports localhost in v1; using 127.0.0.1.";
        }
        if (host == "localhost")
            host = "127.0.0.1";
        const uint16_t port =
            settings.mcpPort <= 0 ? 0u : static_cast<uint16_t>(std::clamp(settings.mcpPort, 1, 65535));
        if (isRunning() && !matchesDesiredEndpoint(host, port))
            stop();
        if (!isRunning())
        {
            std::string error;
            if (start(std::move(host), port, &error))
                ctx.state.statusMessage = "Runtime MCP listening on " + endpoint();
            else
                ctx.state.statusMessage = "Runtime MCP failed: " + error;
        }
    }

    void RuntimeMcpServer::executePending(EditorContext& ctx)
    {
        {
            std::lock_guard lock {m_Mutex};
            for (auto& call : m_DeferredCalls)
                m_PendingCalls.push(std::move(call));
            m_DeferredCalls.clear();
        }

        while (true)
        {
            std::shared_ptr<PendingCall> call;
            {
                std::lock_guard lock {m_Mutex};
                if (m_PendingCalls.empty())
                    break;
                call = m_PendingCalls.front();
                m_PendingCalls.pop();
            }

            // Past the shared deadline the HTTP worker has already stopped waiting, so do not run
            // (or re-run a deferred) call: that would mutate ctx on behalf of a caller that is gone,
            // and a never-resolving deferred condition would otherwise spin every frame forever.
            if (std::chrono::steady_clock::now() >= call->deferDeadline)
            {
                {
                    std::lock_guard callLock {call->mutex};
                    call->result = toolError("runtime MCP tool exceeded deadline before completing");
                    call->done   = true;
                }
                call->cv.notify_one();
                continue;
            }

            nlohmann::json result;
            try
            {
                call->defer = false;
                result      = handleToolCallOnMainThread(call->name, call->args, ctx, call.get());
            }
            catch (const std::exception& e)
            {
                result = toolError(e.what());
            }

            if (call->defer)
            {
                std::lock_guard lock {m_Mutex};
                m_DeferredCalls.push_back(std::move(call));
                continue;
            }

            {
                std::lock_guard callLock {call->mutex};
                call->result = std::move(result);
                call->done   = true;
            }
            call->cv.notify_one();
        }

        captureRecordingFrame(ctx);
        captureVideoStreamFrame(ctx);
    }

    void RuntimeMcpServer::enqueueCall(std::shared_ptr<PendingCall> call)
    {
        {
            std::lock_guard lock {m_Mutex};
            // Set once here, at the same moment the HTTP worker begins its bounded wait, so the
            // main-thread deadline and the client-side timeout share one window.
            call->deferDeadline = std::chrono::steady_clock::now() + kMcpToolDeadline;
            m_PendingCalls.push(std::move(call));
        }
    }

    std::string RuntimeMcpServer::handleHttpRequest(std::string_view requestText)
    {
        const auto headerEnd = requestText.find("\r\n\r\n");
        if (headerEnd == std::string_view::npos)
            return httpResponse(400, "Bad Request", jsonRpcError(nullptr, -32600, "missing HTTP headers").dump());

        const auto firstLineEnd = requestText.find("\r\n");
        const auto firstLine    = requestText.substr(0, firstLineEnd);
        if (!firstLine.starts_with("POST "))
            return httpResponse(405, "Method Not Allowed", jsonRpcError(nullptr, -32600, "MCP endpoint requires POST").dump());

        const auto length = contentLength(requestText.substr(0, headerEnd));
        if (!length.has_value())
            return httpResponse(411, "Length Required", jsonRpcError(nullptr, -32600, "missing Content-Length").dump());

        const auto bodyStart = headerEnd + 4;
        if (requestText.size() < bodyStart + *length)
            return httpResponse(400, "Bad Request", jsonRpcError(nullptr, -32700, "incomplete request body").dump());

        try
        {
            const auto body = requestText.substr(bodyStart, *length);
            const auto request = nlohmann::json::parse(body.begin(), body.end());
            return httpResponse(200, "OK", handleMcpRequest(request).dump());
        }
        catch (const std::exception& e)
        {
            return httpResponse(200, "OK", jsonRpcError(nullptr, -32700, e.what()).dump());
        }
    }

    void RuntimeMcpServer::serverLoop()
    {
        SocketRuntime socketRuntime;
        if (!socketRuntime.ok)
        {
            std::lock_guard lock {m_Mutex};
            m_LastError = "failed to initialize socket runtime";
            m_Running   = false;
            m_StateCv.notify_all();
            return;
        }

        in_addr bindAddress {};
        if (inet_pton(AF_INET, m_Host.c_str(), &bindAddress) != 1)
        {
            std::lock_guard lock {m_Mutex};
            m_LastError = "invalid MCP host: " + m_Host;
            m_Running   = false;
            m_StateCv.notify_all();
            return;
        }

        SocketHandle listener = kInvalidSocket;
        std::string  bindError;
        const auto preferredPort = static_cast<uint32_t>(m_Port);
        const uint32_t attempts = preferredPort == 0 ? 1u : std::min<uint32_t>(17u, 65536u - preferredPort);
        for (uint32_t attempt = 0; attempt < attempts; ++attempt)
        {
            listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (listener == kInvalidSocket)
            {
                bindError = "failed to create socket: " + socketErrorText();
                break;
            }

            int reuse = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

            sockaddr_in addr {};
            addr.sin_family      = AF_INET;
            addr.sin_addr        = bindAddress;
            const uint16_t port  = preferredPort == 0 ? 0 : static_cast<uint16_t>(preferredPort + attempt);
            addr.sin_port        = htons(port);
            if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
            {
                sockaddr_in actual {};
#if defined(_WIN32)
                int actualLen = sizeof(actual);
#else
                socklen_t actualLen = sizeof(actual);
#endif
                if (getsockname(listener, reinterpret_cast<sockaddr*>(&actual), &actualLen) == 0)
                    m_Port = ntohs(actual.sin_port);
                break;
            }

            bindError = "failed to bind " + m_Host + ":" + std::to_string(port) + ": " + socketErrorText();
            closeSocket(listener);
            listener = kInvalidSocket;
        }

        if (listener == kInvalidSocket)
        {
            std::lock_guard lock {m_Mutex};
            m_LastError = bindError.empty() ? "failed to bind runtime MCP socket" : bindError;
            m_Running   = false;
            m_StateCv.notify_all();
            return;
        }
        if (listen(listener, 8) != 0)
        {
            closeSocket(listener);
            std::lock_guard lock {m_Mutex};
            m_LastError = "failed to listen on " + endpoint() + ": " + socketErrorText();
            m_Running   = false;
            m_StateCv.notify_all();
            return;
        }

        {
            std::lock_guard lock {m_Mutex};
            m_Listening = true;
            m_StateCv.notify_all();
        }

        while (true)
        {
            {
                std::lock_guard lock {m_Mutex};
                if (m_StopRequested)
                    break;
            }

            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listener, &readSet);
            timeval timeout {};
            timeout.tv_sec  = 0;
            timeout.tv_usec = 100000;
            const int ready = select(static_cast<int>(listener + 1), &readSet, nullptr, nullptr, &timeout);
            if (ready <= 0 || !FD_ISSET(listener, &readSet))
                continue;

            sockaddr_in clientAddr {};
#if defined(_WIN32)
            int clientLen = sizeof(clientAddr);
#else
            socklen_t clientLen = sizeof(clientAddr);
#endif
            SocketHandle client = accept(listener, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (client == kInvalidSocket)
                continue;

            std::string request;
            std::array<char, 4096> buffer {};
            std::optional<size_t> expectedBody;
            size_t headerEnd = std::string::npos;
            while (request.size() < 1024u * 1024u)
            {
                fd_set clientReadSet;
                FD_ZERO(&clientReadSet);
                FD_SET(client, &clientReadSet);
                timeval clientTimeout {};
                clientTimeout.tv_sec  = 2;
                clientTimeout.tv_usec = 0;
                const int clientReady =
                    select(static_cast<int>(client + 1), &clientReadSet, nullptr, nullptr, &clientTimeout);
                if (clientReady <= 0 || !FD_ISSET(client, &clientReadSet))
                    break;

#if defined(_WIN32)
                const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
                const ssize_t received = recv(client, buffer.data(), buffer.size(), 0);
#endif
                if (received <= 0)
                    break;
                request.append(buffer.data(), static_cast<size_t>(received));
                headerEnd = request.find("\r\n\r\n");
                if (headerEnd != std::string::npos)
                {
                    if (isGetRequest(request))
                        break;
                    expectedBody = contentLength(std::string_view {request}.substr(0, headerEnd));
                    if (expectedBody.has_value() && request.size() >= headerEnd + 4 + *expectedBody)
                        break;
                }
            }

            if (isGetRequest(request))
            {
                const auto target = requestTarget(request);
                constexpr std::string_view kStreamPrefix {"/stream/"};
                if (!target.starts_with(kStreamPrefix))
                {
                    (void)sendAll(client, httpTextResponse(404, "Not Found", "text/plain", "not found"));
                    closeSocket(client);
                    continue;
                }

                const auto streamId = target.substr(kStreamPrefix.size());
                std::thread streamThread {[this, client, streamId] {
                    auto closeClient = [&]() { closeSocket(client); };
                    {
                        std::lock_guard lock {m_VideoStreamMutex};
                        if (!m_VideoStream.active || m_VideoStream.id != streamId)
                        {
                            (void)sendAll(client, httpTextResponse(404, "Not Found", "text/plain", "stream not active"));
                            closeClient();
                            return;
                        }
                    }

                    std::ostringstream headers;
                    headers << "HTTP/1.1 200 OK\r\n"
                            << "Content-Type: multipart/x-mixed-replace; boundary=vultra-frame\r\n"
                            << "Cache-Control: no-cache\r\n"
                            << "Connection: close\r\n\r\n";
                    if (!sendAll(client, headers.str()))
                    {
                        closeClient();
                        return;
                    }

                    uint64_t lastSequence = 0;
                    while (true)
                    {
                        std::vector<uint8_t> jpeg;
                        uint64_t sequence = 0;
                        {
                            std::unique_lock lock {m_VideoStreamMutex};
                            m_VideoStreamCv.wait_for(lock, std::chrono::seconds(2), [&] {
                                return !m_VideoStream.active || m_VideoStream.id != streamId ||
                                       m_VideoStream.sequence != lastSequence;
                            });
                            if (!m_VideoStream.active || m_VideoStream.id != streamId)
                                break;
                            if (m_VideoStream.sequence == lastSequence || m_VideoStream.latestJpeg.empty())
                                continue;
                            jpeg = m_VideoStream.latestJpeg;
                            sequence = m_VideoStream.sequence;
                        }

                        std::ostringstream part;
                        part << "--vultra-frame\r\n"
                             << "Content-Type: image/jpeg\r\n"
                             << "Content-Length: " << jpeg.size() << "\r\n"
                             << "X-Vultra-Frame: " << sequence << "\r\n\r\n";
                        if (!sendAll(client, part.str()) ||
                            !sendAll(client, std::string_view(reinterpret_cast<const char*>(jpeg.data()), jpeg.size())) ||
                            !sendAll(client, "\r\n"))
                        {
                            break;
                        }
                        lastSequence = sequence;
                    }
                    closeClient();
                }};
                {
                    std::lock_guard lock {m_VideoStreamClientMutex};
                    m_VideoStreamClientThreads.push_back(std::move(streamThread));
                }
                continue;
            }

            const auto response = handleHttpRequest(request);
            (void)sendAll(client, response);
            closeSocket(client);
        }

        closeSocket(listener);
        std::lock_guard lock {m_Mutex};
        m_Running = false;
        m_Listening = false;
        m_StateCv.notify_all();
    }
} // namespace vultra_app
