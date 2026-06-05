#include "editor_app/runtime_mcp_server.hpp"
#include "editor_app/runtime_mcp_server_internal.hpp"
#include "editor_app/runtime_mcp_tool_registry.hpp"

#include "app_state.hpp"
#include "editor_app/editor_app.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vultra_app
{
    namespace
    {
        constexpr std::string_view kProtocolVersion {"2025-03-26"};

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

        // Forward an MCP tool straight to a named editor command. This is the shared body that
        // every thin "vultra.*" pass-through tool used to inline verbatim.
        nlohmann::json dispatchEditorCommand(EditorContext& ctx, std::string_view commandName, const nlohmann::json& args)
        {
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, std::string(commandName), args);
            if (!result.value("ok", false))
                return toolError(result.value("error", std::string(commandName) + " failed"));
            return toolJson(std::move(result));
        }

        // Single source of truth for the thin pass-through tools: MCP tool name -> editor command
        // name. Most map 1:1 (prefix dropped); the few that differ (e.g. scene.save) are explicit.
        // Tools with bespoke logic (command, command_batch) stay as special cases below.
        const std::unordered_map<std::string_view, std::string_view> kEditorCommandTools {
            {"vultra.editor.back_to_launcher", "editor.back_to_launcher"},
            {"vultra.editor.window", "editor.window"},
            {"vultra.project.create_empty", "project.create_empty"},
            {"vultra.scene.new", "scene.new"},
            {"vultra.scene.list_entity_kinds", "scene.list_entity_kinds"},
            {"vultra.scene.list_component_kinds", "scene.list_component_kinds"},
            {"vultra.scene.component_metadata", "scene.component_metadata"},
            {"vultra.scene.get_component", "scene.get_component"},
            {"vultra.scene.add_entity", "scene.add_entity"},
            {"vultra.scene.remove_entity", "scene.remove_entity"},
            {"vultra.scene.add_component", "scene.add_component"},
            {"vultra.scene.update_component", "scene.update_component"},
            {"vultra.scene.remove_component", "scene.remove_component"},
            {"vultra.scene.select_entity", "scene.select_entity"},
            {"vultra.scene.move_entity", "scene.move_entity"},
            {"vultra.scene.instantiate_asset", "scene.instantiate_asset"},
            {"vultra.scene.save", "editor.save_scene"},
        };

        // tool name -> published inputSchema, built once from the same toolsList() that
        // tools/list returns, so validation and the advertised schema cannot drift apart.
        const std::unordered_map<std::string, nlohmann::json>& toolSchemas()
        {
            static const std::unordered_map<std::string, nlohmann::json> schemas = [] {
                std::unordered_map<std::string, nlohmann::json> map;
                const auto list = runtime_mcp::toolsList();
                if (list.contains("tools") && list["tools"].is_array())
                {
                    for (const auto& tool : list["tools"])
                    {
                        if (tool.contains("name") && tool["name"].is_string())
                            map.emplace(tool["name"].get<std::string>(),
                                        tool.value("inputSchema", nlohmann::json::object()));
                    }
                }
                return map;
            }();
            return schemas;
        }

        bool jsonMatchesSchemaType(const nlohmann::json& value, std::string_view type)
        {
            if (type == "string")
                return value.is_string();
            if (type == "boolean")
                return value.is_boolean();
            if (type == "object")
                return value.is_object();
            if (type == "array")
                return value.is_array();
            if (type == "integer" || type == "number")
                return value.is_number();
            return true; // unknown/unspecified type: do not reject
        }

        // Minimal pre-dispatch validation against the published inputSchema: required fields
        // must be present, and any provided field declared with a type must match it. Returns an
        // actionable message on failure, or nullopt when the call looks well-formed. Unknown
        // tools are not validated here; dispatch reports them.
        std::optional<std::string> validateToolArgs(const std::string& name, const nlohmann::json& args)
        {
            const auto& schemas = toolSchemas();
            const auto  it      = schemas.find(name);
            if (it == schemas.end())
                return std::nullopt;
            const auto& schema = it->second;

            std::vector<std::string> problems;

            if (schema.contains("required") && schema["required"].is_array())
            {
                for (const auto& required : schema["required"])
                {
                    if (required.is_string() && !args.contains(required.get<std::string>()))
                        problems.push_back("missing required field '" + required.get<std::string>() + "'");
                }
            }

            if (schema.contains("properties") && schema["properties"].is_object() && args.is_object())
            {
                const auto& properties = schema["properties"];
                for (auto field = args.begin(); field != args.end(); ++field)
                {
                    const auto prop = properties.find(field.key());
                    if (prop == properties.end() || !prop->is_object() || !prop->contains("type") ||
                        !(*prop)["type"].is_string())
                        continue; // unknown/extra field or untyped schema entry: allowed
                    const auto expected = (*prop)["type"].get<std::string>();
                    if (!jsonMatchesSchemaType(field.value(), expected))
                        problems.push_back("field '" + field.key() + "' should be of type " + expected);
                }
            }

            if (problems.empty())
                return std::nullopt;

            std::string message = "invalid arguments for " + name + ": ";
            for (std::size_t i = 0; i < problems.size(); ++i)
                message += (i == 0 ? "" : "; ") + problems[i];
            return message;
        }

        // Capability gating. The agent reaches these tools autonomously (Claude -> stdio bridge ->
        // HTTP), so the real guard for state-changing tools lives here at the execution layer, keyed
        // off the user's EditorSettings switches. Read-only tools (status/list/get/capture/raycast/
        // profiler/...) are intentionally NOT listed and are always permitted. Anything that mutates
        // the simulation/scene/editor session is an engine op; anything that writes the project/assets
        // on disk is a project op. The arbitrary command executors are gated as engine ops (strict).
        enum class ToolCategory
        {
            ReadOnly,
            EngineOp,
            ProjectOp,
        };

        ToolCategory categorizeTool(std::string_view name)
        {
            static const std::unordered_set<std::string_view> kProjectOps {
                "vultra.project.create_empty",
                "vultra.scene.save",
                "vultra.assets.write",
                "vultra.assets.import",
                "vultra.assets.import_from_web",
                "vultra.assets.import_package",
                "vultra.material_graph.compile",
            };
            static const std::unordered_set<std::string_view> kEngineOps {
                "vultra.sim.reset",
                "vultra.sim.step",
                "vultra.sim.set_state_batch",
                "vultra.sim.apply_actions_batch",
                "vultra.animator.set_param",
                "vultra.editor.command",
                "vultra.editor.command_batch",
                "vultra.editor.back_to_launcher",
                "vultra.editor.recording",
                "vultra.editor.input",
                "vultra.editor.quit",
                "vultra.scene.new",
                "vultra.scene.add_entity",
                "vultra.scene.remove_entity",
                "vultra.scene.add_component",
                "vultra.scene.update_component",
                "vultra.scene.remove_component",
                "vultra.scene.move_entity",
                "vultra.scene.instantiate_asset",
                "vultra.runtime.playback",
                "vultra.runtime.reload_pipeline",
            };
            if (kProjectOps.contains(name))
                return ToolCategory::ProjectOp;
            if (kEngineOps.contains(name))
                return ToolCategory::EngineOp;
            return ToolCategory::ReadOnly;
        }

        std::optional<std::string> agentCapabilityDenial(std::string_view name,
                                                         const AppState::EditorSettings& settings)
        {
            switch (categorizeTool(name))
            {
                case ToolCategory::EngineOp:
                    if (!settings.allowAgentEngineOperations)
                        return "engine operations are disabled for the agent; enable 'Allow Engine "
                               "Operations' in Editor Settings to run " +
                               std::string(name);
                    break;
                case ToolCategory::ProjectOp:
                    if (!settings.allowAgentProjectOperations)
                        return "project operations are disabled for the agent; enable 'Allow Project "
                               "Operations' in Editor Settings to run " +
                               std::string(name);
                    break;
                case ToolCategory::ReadOnly:
                    break;
            }
            return std::nullopt;
        }

        // Anthropic tool names must match ^[a-zA-Z0-9_-]+$, but tools are registered with dotted
        // names like "vultra.runtime.status". When a client connects directly over HTTP (no stdio
        // bridge to translate), the server must therefore advertise underscore names and map them
        // back to the dotted originals on tools/call. The dispatch layer keeps using dotted names.
        std::string sanitizeToolName(const std::string& name)
        {
            std::string out = name;
            for (char& c : out)
            {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                c == '_' || c == '-';
                if (!ok)
                    c = '_';
            }
            return out;
        }

        // sanitized name -> original dotted name, built once from the registry.
        const std::unordered_map<std::string, std::string>& sanitizedToOriginalNames()
        {
            static const std::unordered_map<std::string, std::string> map = [] {
                std::unordered_map<std::string, std::string> m;
                const auto list = runtime_mcp::toolsList();
                if (list.contains("tools") && list["tools"].is_array())
                {
                    for (const auto& tool : list["tools"])
                    {
                        if (tool.contains("name") && tool["name"].is_string())
                        {
                            auto original = tool["name"].get<std::string>();
                            m.emplace(sanitizeToolName(original), std::move(original));
                        }
                    }
                }
                return m;
            }();
            return map;
        }

        // tools/list with every advertised name sanitized to a client-safe form.
        nlohmann::json sanitizedToolsList()
        {
            auto list = runtime_mcp::toolsList();
            if (list.contains("tools") && list["tools"].is_array())
            {
                for (auto& tool : list["tools"])
                {
                    if (tool.contains("name") && tool["name"].is_string())
                        tool["name"] = sanitizeToolName(tool["name"].get<std::string>());
                }
            }
            return list;
        }

        std::string originalToolName(const std::string& name)
        {
            const auto& map = sanitizedToOriginalNames();
            if (const auto it = map.find(name); it != map.end())
                return it->second;
            return name; // already an original (dotted) name, or unknown: pass through
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
            // Echo the client's requested protocol version so a newer client does not reject the
            // connection over a version mismatch.
            const auto initParams = request.value("params", nlohmann::json::object());
            const auto protocol   = initParams.is_object()
                                        ? initParams.value("protocolVersion", std::string {kProtocolVersion})
                                        : std::string {kProtocolVersion};
            return jsonRpcResult(id,
                                 {{"protocolVersion", protocol},
                                  {"capabilities", {{"tools", nlohmann::json::object()}}},
                                  {"serverInfo", {{"name", "vultra-runtime-mcp"}, {"version", "0.1.0"}}}});
        }
        if (method == "tools/list")
            return jsonRpcResult(id, sanitizedToolsList());
        if (method != "tools/call")
            return jsonRpcError(id, -32601, "method not found: " + method);

        const auto params = request.value("params", nlohmann::json::object());
        if (!params.is_object() || !params.contains("name") || !params["name"].is_string())
            return jsonRpcError(id, -32602, "tools/call requires a string name");

        auto call  = std::make_shared<PendingCall>();
        // Map the client-safe (underscore) name back to the registered dotted name for dispatch.
        call->name = originalToolName(params["name"].get<std::string>());
        call->args = params.value("arguments", nlohmann::json::object());

        // Fail fast with an actionable message before occupying the main thread.
        if (auto validationError = validateToolArgs(call->name, call->args))
            return jsonRpcResult(id, toolError(std::move(*validationError)));

        enqueueCall(call);

        std::unique_lock callLock {call->mutex};
        if (!call->cv.wait_for(callLock, kMcpToolDeadline, [&] { return call->done; }))
            return jsonRpcError(id, -32000, "runtime MCP tool timed out waiting for main thread");
        return jsonRpcResult(id, call->result);
    }

    nlohmann::json RuntimeMcpServer::handleToolCallOnMainThread(std::string_view name,
                                                                const nlohmann::json& args,
                                                                EditorContext& ctx,
                                                                PendingCall* call)
    {
        // Enforce the agent capability switches before any tool runs (see categorizeTool).
        if (auto denial = agentCapabilityDenial(name, ctx.state.editorSettings))
            return toolError(std::move(*denial));

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

        if (const auto it = kEditorCommandTools.find(name); it != kEditorCommandTools.end())
            return dispatchEditorCommand(ctx, it->second, args);

        if (auto result = handleAssetTool(name, args, ctx); !result.is_null())
            return result;

        if (auto result = handleEditorAutomationTool(name, args, ctx); !result.is_null())
            return result;
        return toolError("unknown tool: " + std::string(name));
    }

} // namespace vultra_app
