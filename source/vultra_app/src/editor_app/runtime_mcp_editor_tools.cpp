#include "editor_app/runtime_mcp_server.hpp"

#include <vultra/core/rhi/structs/pixel_format.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>

#include <nlohmann/json.hpp>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace vultra_app
{
    namespace
    {
        std::string lowerAscii(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
        }

        nlohmann::json toolJson(nlohmann::json payload)
        {
            return {{"content", nlohmann::json::array({{{"type", "text"}, {"text", payload.dump(2)}}})}};
        }

        nlohmann::json toolError(std::string message)
        {
            return {{"isError", true},
                    {"content",
                     nlohmann::json::array({{{"type", "text"},
                                              {"text", nlohmann::json({{"ok", false}, {"error", std::move(message)}}).dump(2)}}})}};
        }

        std::string timestampForPath()
        {
            const auto now = std::chrono::system_clock::now();
            const auto t   = std::chrono::system_clock::to_time_t(now);
            std::tm tm {};
#if defined(_WIN32)
            localtime_s(&tm, &t);
#else
            localtime_r(&tm, &t);
#endif
            std::ostringstream out;
            out << std::put_time(&tm, "%Y%m%d_%H%M%S");
            return out.str();
        }

        std::string shellQuote(const std::filesystem::path& path)
        {
            auto text = path.generic_string();
            std::string escaped;
            escaped.reserve(text.size() + 2);
            escaped.push_back('"');
            for (const char ch : text)
            {
                if (ch == '"')
                    escaped += "\\\"";
                else
                    escaped.push_back(ch);
            }
            escaped.push_back('"');
            return escaped;
        }

        FILE* openPipeWrite(const std::string& command)
        {
#if defined(_WIN32)
            return _popen(command.c_str(), "wb");
#else
            return popen(command.c_str(), "w");
#endif
        }

        std::string rawVideoPixelFormat(const vultra::rhi::PixelFormat format)
        {
            switch (format)
            {
                case vultra::rhi::PixelFormat::eBGRA8_UNorm: return "bgra";
                case vultra::rhi::PixelFormat::eRGBA8_UNorm: return "rgba";
                default: return {};
            }
        }
    } // namespace

    nlohmann::json RuntimeMcpServer::handleEditorAutomationTool(std::string_view name,
                                                                const nlohmann::json& args,
                                                                EditorContext& ctx)
    {        if (name == "vultra.editor.capture")
        {
            auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
            if (!backendService)
                return toolError("render backend service is unavailable");
            const auto outputFile =
                std::filesystem::path(args.value("outputFile", std::string {".vultra/mcp/editor_frame.png"}));
            std::error_code ec;
            std::filesystem::create_directories(outputFile.parent_path(), ec);
            if (ec)
                return toolError("failed to create output directory: " + ec.message());
            auto& backbuffer = backendService->backbuffer();
            const bool ok = backendService->renderDevice().saveTextureToFile(backbuffer, outputFile.generic_string());
            return toolJson({{"ok", ok},
                             {"file", outputFile.generic_string()},
                             {"extent", {{"width", backbuffer.getExtent().width}, {"height", backbuffer.getExtent().height}}},
                             {"format", std::string(vultra::rhi::toString(backbuffer.getPixelFormat()))}});
        }

        if (name == "vultra.editor.recording")
        {
            const auto action = lowerAscii(args.value("action", std::string {}));
            if (action.empty())
                return toolError("recording requires action");

            const auto recordingJson = [this]() {
                const double elapsedSeconds =
                    m_Recording.startedAt == std::chrono::steady_clock::time_point {} ?
                        0.0 :
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - m_Recording.startedAt).count();
                return nlohmann::json {{"active", m_Recording.active},
                                       {"fps", m_Recording.fps},
                                       {"frameCount", m_Recording.frameCount},
                                       {"maxFrames", m_Recording.maxFrames},
                                       {"outputFile", m_Recording.outputFile},
                                       {"frameDirectory", m_Recording.frameDirectory},
                                       {"mode", m_Recording.mode},
                                       {"streamPixelFormat", m_Recording.streamPixelFormat},
                                       {"ffmpegCommand", m_Recording.ffmpegCommand},
                                       {"width", m_Recording.width},
                                       {"height", m_Recording.height},
                                       {"droppedFrames", m_Recording.droppedFrames},
                                       {"queuedFrames", m_Recording.queuedFrames},
                                       {"maxQueuedFrames", m_Recording.maxQueuedFrames},
                                       {"lastFrameFile", m_Recording.lastFrameFile},
                                       {"lastError", m_Recording.lastError},
                                       {"elapsedSeconds", elapsedSeconds}};
            };

            if (action == "status")
                return toolJson({{"ok", true}, {"recording", recordingJson()}});

            if (action == "start")
            {
                if (m_Recording.active)
                    return toolError("recording is already active");
                auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
                if (!backendService)
                    return toolError("render backend service is unavailable");
                auto& backbuffer = backendService->backbuffer();

                const auto stamp = timestampForPath();
                const auto outputFile =
                    std::filesystem::path(args.value("outputFile", ".vultra/mcp/recordings/recording_" + stamp + ".mp4"))
                        .lexically_normal();
                const auto mode = lowerAscii(args.value("mode", std::string {"stream"}));
                if (mode != "stream" && mode != "frames")
                    return toolError("recording mode must be stream or frames");
                std::filesystem::path frameDirectory =
                    std::filesystem::path(args.value("frameDirectory", std::string {})).lexically_normal();
                if (frameDirectory.empty())
                    frameDirectory = (outputFile.parent_path() / (outputFile.stem().generic_string() + "_frames")).lexically_normal();

                std::error_code ec;
                if (!outputFile.parent_path().empty())
                    std::filesystem::create_directories(outputFile.parent_path(), ec);
                if (ec)
                    return toolError("failed to create recording output directory: " + ec.message());
                if (mode == "frames")
                {
                    std::filesystem::create_directories(frameDirectory, ec);
                    if (ec)
                        return toolError("failed to create recording frame directory: " + ec.message());
                }

                m_Recording                 = {};
                m_Recording.active          = true;
                m_Recording.encodeOnStop    = args.value("encodeOnStop", true);
                m_Recording.fps             = std::clamp(args.value("fps", 30), 1, 120);
                m_Recording.maxFrames       = static_cast<uint32_t>(std::max(args.value("maxFrames", 0), 0));
                m_Recording.outputFile      = outputFile.generic_string();
                m_Recording.frameDirectory  = frameDirectory.generic_string();
                m_Recording.mode            = mode;
                m_Recording.width           = backbuffer.getExtent().width;
                m_Recording.height          = backbuffer.getExtent().height;
                m_Recording.maxQueuedFrames = static_cast<uint32_t>(std::clamp(args.value("maxQueuedFrames", 3), 1, 16));
                m_Recording.startedAt       = std::chrono::steady_clock::now();
                m_Recording.lastFrameAt     = {};
                if (mode == "stream")
                {
                    m_Recording.streamPixelFormat = rawVideoPixelFormat(backbuffer.getPixelFormat());
                    if (m_Recording.streamPixelFormat.empty())
                    {
                        m_Recording = {};
                        return toolError("recording stream does not support backbuffer format: " +
                                         std::string(vultra::rhi::toString(backbuffer.getPixelFormat())));
                    }
                    std::ostringstream cmd;
                    cmd << "ffmpeg -y -loglevel error"
                        << " -f rawvideo"
                        << " -pixel_format " << m_Recording.streamPixelFormat
                        << " -video_size " << std::max(m_Recording.width, 1u) << "x" << std::max(m_Recording.height, 1u)
                        << " -framerate " << std::max(m_Recording.fps, 1)
                        << " -i - -pix_fmt yuv420p " << shellQuote(outputFile);
                    m_Recording.ffmpegCommand = cmd.str();
                    m_RecordingPipe = openPipeWrite(m_Recording.ffmpegCommand);
                    if (!m_RecordingPipe)
                    {
                        m_Recording = {};
                        return toolError("failed to open ffmpeg stream pipe");
                    }
                    startRecordingWriter();
                    m_Recording.encodeOnStop = false;
                }
                captureRecordingFrame(ctx);
                return toolJson({{"ok", true}, {"recording", recordingJson()}});
            }

            if (action == "stop")
            {
                if (!m_Recording.active && m_Recording.frameCount == 0)
                    return toolError("recording is not active");
                m_Recording.active = false;

                bool        encoded = false;
                int         encodeExitCode = 0;
                std::string encodeCommand;
                std::string encodeError;
                if (m_Recording.mode == "stream")
                {
                    encodeCommand = m_Recording.ffmpegCommand;
                    encodeExitCode = stopRecordingWriter();
                    encoded = encodeExitCode == 0;
                    if (!encoded)
                        encodeError = "ffmpeg stream encode failed";
                }
                else if (m_Recording.encodeOnStop && !m_Recording.outputFile.empty() && m_Recording.frameCount > 0)
                {
                    const auto framePattern =
                        std::filesystem::path(m_Recording.frameDirectory) / "frame_%06d.png";
                    const auto outputFile = std::filesystem::path(m_Recording.outputFile);
                    std::error_code ec;
                    if (!outputFile.parent_path().empty())
                        std::filesystem::create_directories(outputFile.parent_path(), ec);
                    if (ec)
                    {
                        encodeError = "failed to create video output directory: " + ec.message();
                    }
                    else
                    {
                        std::ostringstream cmd;
                        cmd << "ffmpeg -y -loglevel error -framerate " << std::max(m_Recording.fps, 1)
                            << " -i " << shellQuote(framePattern)
                            << " -pix_fmt yuv420p " << shellQuote(outputFile);
                        encodeCommand  = cmd.str();
                        encodeExitCode = std::system(encodeCommand.c_str());
                        encoded        = encodeExitCode == 0;
                        if (!encoded)
                            encodeError = "ffmpeg failed or is not available";
                    }
                }

                auto response = recordingJson();
                response["ok"] = true;
                response["encoded"] = encoded;
                response["encodeExitCode"] = encodeExitCode;
                response["encodeError"] = encodeError;
                response["encodeCommand"] = encodeCommand;
                return toolJson(std::move(response));
            }

            return toolError("unsupported recording action: " + action);
        }

        if (name == "vultra.editor.input")
        {
            const auto action = args.value("action", std::string {});
            auto&      io     = ImGui::GetIO();
            if (action == "mouse_move")
            {
                io.AddMousePosEvent(static_cast<float>(args.value("x", 0.0)), static_cast<float>(args.value("y", 0.0)));
            }
            else if (action == "mouse_button")
            {
                io.AddMouseButtonEvent(std::clamp(args.value("button", 0), 0, 4), args.value("down", true));
            }
            else if (action == "mouse_wheel")
            {
                io.AddMouseWheelEvent(static_cast<float>(args.value("x", 0.0)), static_cast<float>(args.value("y", 0.0)));
            }
            else if (action == "text")
            {
                io.AddInputCharactersUTF8(args.value("text", std::string {}).c_str());
            }
            else if (action == "key")
            {
                const auto keyName = lowerAscii(args.value("key", std::string {}));
                const bool down    = args.value("down", true);
                if (keyName == "ctrl")
                {
                    io.KeyCtrl = down;
                    return toolJson({{"ok", true}, {"action", action}, {"target", "imgui"}});
                }
                if (keyName == "shift")
                {
                    io.KeyShift = down;
                    return toolJson({{"ok", true}, {"action", action}, {"target", "imgui"}});
                }
                if (keyName == "alt")
                {
                    io.KeyAlt = down;
                    return toolJson({{"ok", true}, {"action", action}, {"target", "imgui"}});
                }
                static const std::unordered_map<std::string, ImGuiKey> kKeys {
                    {"enter", ImGuiKey_Enter}, {"escape", ImGuiKey_Escape}, {"tab", ImGuiKey_Tab},
                    {"space", ImGuiKey_Space}, {"backspace", ImGuiKey_Backspace}, {"delete", ImGuiKey_Delete},
                    {"left", ImGuiKey_LeftArrow}, {"right", ImGuiKey_RightArrow}, {"up", ImGuiKey_UpArrow},
                    {"down", ImGuiKey_DownArrow}, {"a", ImGuiKey_A}, {"c", ImGuiKey_C}, {"v", ImGuiKey_V},
                    {"x", ImGuiKey_X}, {"y", ImGuiKey_Y}, {"z", ImGuiKey_Z}, {"s", ImGuiKey_S},
                };
                const auto it = kKeys.find(keyName);
                if (it == kKeys.end())
                    return toolError("unsupported key: " + keyName);
                io.AddKeyEvent(it->second, down);
            }
            else
            {
                return toolError("unsupported editor input action: " + action);
            }
            return toolJson({{"ok", true}, {"action", action}, {"target", "imgui"}});
        }

        if (name == "vultra.editor.quit")
        {
            auto* windowService = ctx.services ? ctx.services->tryGet<IWindowService>() : nullptr;
            if (!windowService)
                return toolError("window service is unavailable");
            windowService->window().close();
            return toolJson({{"ok", true}, {"closing", true}});
        }


        return nullptr;
    }
} // namespace vultra_app
