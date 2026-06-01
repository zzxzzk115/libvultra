#include "editor_app/runtime_mcp_server.hpp"
#include "editor_app/runtime_mcp_server_internal.hpp"
#include "editor_app/runtime_mcp_tool_registry.hpp"

#include "editor_app/editor_app.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace vultra_app
{
    namespace
    {
        constexpr std::string_view kProtocolVersion {"2025-03-26"};
        constexpr auto             kToolTimeout = std::chrono::seconds(5);

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

    } // namespace

    nlohmann::json RuntimeMcpServer::handleMcpRequest(const nlohmann::json& request)
    {
        const auto id = request.contains("id") ? request["id"] : nlohmann::json(nullptr);
        if (!request.is_object() || !request.contains("method") || !request["method"].is_string())
            return jsonRpcError(id, -32600, "invalid MCP request");

        const auto method = request["method"].get<std::string>();
        if (method == "initialize")
        {
            return jsonRpcResult(id,
                                 {{"protocolVersion", kProtocolVersion},
                                  {"capabilities", {{"tools", nlohmann::json::object()}}},
                                  {"serverInfo", {{"name", "vultra-runtime-mcp"}, {"version", "0.1.0"}}}});
        }
        if (method == "tools/list")
            return jsonRpcResult(id, runtime_mcp::toolsList());
        if (method != "tools/call")
            return jsonRpcError(id, -32601, "method not found: " + method);

        const auto params = request.value("params", nlohmann::json::object());
        if (!params.is_object() || !params.contains("name") || !params["name"].is_string())
            return jsonRpcError(id, -32602, "tools/call requires a string name");

        auto call  = std::make_shared<PendingCall>();
        call->name = params["name"].get<std::string>();
        call->args = params.value("arguments", nlohmann::json::object());
        enqueueCall(call);

        std::unique_lock callLock {call->mutex};
        if (!call->cv.wait_for(callLock, kToolTimeout, [&] { return call->done; }))
            return jsonRpcError(id, -32000, "runtime MCP tool timed out waiting for main thread");
        return jsonRpcResult(id, call->result);
    }

    nlohmann::json RuntimeMcpServer::handleToolCallOnMainThread(std::string_view name,
                                                                const nlohmann::json& args,
                                                                EditorContext& ctx,
                                                                PendingCall* call)
    {
        if (auto result = handleRuntimeTool(name, args, ctx, call); !result.is_null())
            return result;

        if (auto result = handleSimTool(name, args, ctx, call); !result.is_null())
            return result;

        if (name == "vultra.editor.command")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            const auto commandName = args.value("name", std::string {});
            if (commandName.empty())
                return toolError("editor.command requires name");
            const auto commandArgs =
                args.contains("arguments") && args["arguments"].is_object() ? args["arguments"] : nlohmann::json::object();
            auto result = ctx.editor->executeCommand(ctx, commandName, commandArgs);
            if (!result.value("ok", false))
                return toolError(result.value("error", "editor command failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.editor.command_batch")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            if (!args.contains("commands") || !args["commands"].is_array())
                return toolError("editor.command_batch requires commands array");

            const bool stopOnError = args.value("stopOnError", true);
            nlohmann::json results = nlohmann::json::array();
            bool allOk = true;
            for (const auto& command : args["commands"])
            {
                if (!command.is_object())
                {
                    allOk = false;
                    results.push_back({{"ok", false}, {"error", "batch command must be an object"}});
                    if (stopOnError)
                        break;
                    continue;
                }
                const auto commandName = command.value("name", std::string {});
                const auto commandArgs = command.contains("arguments") && command["arguments"].is_object() ?
                                             command["arguments"] :
                                             nlohmann::json::object();
                if (commandName.empty())
                {
                    allOk = false;
                    results.push_back({{"ok", false}, {"error", "batch command requires name"}});
                    if (stopOnError)
                        break;
                    continue;
                }

                auto result = ctx.editor->executeCommand(ctx, commandName, commandArgs);
                result["name"] = commandName;
                if (!result.value("ok", false))
                    allOk = false;
                results.push_back(std::move(result));
                if (!allOk && stopOnError)
                    break;
            }

            auto payload = nlohmann::json {{"ok", allOk}, {"results", std::move(results)}};
            if (!allOk)
                payload["error"] = "one or more editor commands failed";
            return toolJson(std::move(payload));
        }

        if (name == "vultra.editor.back_to_launcher")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "editor.back_to_launcher", nlohmann::json::object());
            if (!result.value("ok", false))
                return toolError(result.value("error", "editor.back_to_launcher failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.editor.window")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "editor.window", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "editor.window failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.project.create_empty")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "project.create_empty", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "project.create_empty failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.new")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "scene.new", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "scene.new failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.list_entity_kinds")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "scene.list_entity_kinds", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "scene.list_entity_kinds failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.list_component_kinds")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "scene.list_component_kinds", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "scene.list_component_kinds failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.add_entity")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "scene.add_entity", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "scene.add_entity failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.remove_entity")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "scene.remove_entity", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "scene.remove_entity failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.add_component")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "scene.add_component", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "scene.add_component failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.update_component")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "scene.update_component", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "scene.update_component failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.remove_component")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "scene.remove_component", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "scene.remove_component failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.select_entity")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "scene.select_entity", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "scene.select_entity failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.move_entity")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "scene.move_entity", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "scene.move_entity failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.instantiate_asset")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "scene.instantiate_asset", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "scene.instantiate_asset failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.scene.save")
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "editor.save_scene", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "editor.save_scene failed"));
            return toolJson(std::move(result));
        }

        if (auto result = handleAssetTool(name, args, ctx); !result.is_null())
            return result;

        if (auto result = handleEditorAutomationTool(name, args, ctx); !result.is_null())
            return result;
        return toolError("unknown tool: " + std::string(name));
    }

} // namespace vultra_app
