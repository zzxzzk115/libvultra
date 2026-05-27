#include "editor_app/editor_settings_persistence.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace vultra_app
{
    namespace
    {
        float jsonFloat(const nlohmann::json& json, const char* key, const float fallback)
        {
            const auto it = json.find(key);
            if (it == json.end() || !it->is_number())
                return fallback;
            return it->get<float>();
        }

        int jsonInt(const nlohmann::json& json, const char* key, const int fallback)
        {
            const auto it = json.find(key);
            if (it == json.end() || !it->is_number_integer())
                return fallback;
            return it->get<int>();
        }

        bool jsonBool(const nlohmann::json& json, const char* key, const bool fallback)
        {
            const auto it = json.find(key);
            if (it == json.end() || !it->is_boolean())
                return fallback;
            return it->get<bool>();
        }

        std::string jsonString(const nlohmann::json& json, const char* key, std::string fallback)
        {
            const auto it = json.find(key);
            if (it == json.end() || !it->is_string())
                return fallback;
            return it->get<std::string>();
        }

        glm::vec4 jsonVec4(const nlohmann::json& json, const char* key, const glm::vec4& fallback)
        {
            const auto it = json.find(key);
            if (it == json.end() || !it->is_array() || it->size() < 4)
                return fallback;

            glm::vec4 value = fallback;
            for (size_t i = 0; i < 4; ++i)
            {
                if (!(*it)[i].is_number())
                    return fallback;
                value[static_cast<glm::vec4::length_type>(i)] = (*it)[i].get<float>();
            }
            return value;
        }

        nlohmann::json jsonVec4Value(const glm::vec4& value)
        {
            return nlohmann::json::array({value.x, value.y, value.z, value.w});
        }

        void normalizeEditorSettings(AppState::EditorSettings& settings)
        {
            constexpr std::array validThemes {"Dark", "Graphite", "Light", "Custom"};
            if (std::find(validThemes.begin(), validThemes.end(), settings.theme) == validThemes.end())
                settings.theme = "Dark";

            settings.applicationScale = std::clamp(settings.applicationScale, 0.75f, 2.0f);
            settings.textScale        = std::clamp(settings.textScale, 0.75f, 2.0f);
            settings.interfaceFontSize = std::clamp(settings.interfaceFontSize, 10, 24);
            settings.monospaceFontSize = std::clamp(settings.monospaceFontSize, 10, 24);
        }
    } // namespace

    bool loadEditorSettings(const std::filesystem::path& path,
                            AppState::EditorSettings&   settings,
                            std::string*                error)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        if (!fs::exists(path, ec))
            return true;
        if (ec)
        {
            if (error)
                *error = "failed to inspect editor settings: " + ec.message();
            return false;
        }

        std::ifstream file(path);
        if (!file)
        {
            if (error)
                *error = "failed to open editor settings: " + path.generic_string();
            return false;
        }

        try
        {
            nlohmann::json json;
            file >> json;
            if (!json.is_object())
                throw std::runtime_error("root must be an object");

            AppState::EditorSettings loaded = settings;
            loaded.applicationScale         = jsonFloat(json, "applicationScale", loaded.applicationScale);
            loaded.textScale                = jsonFloat(json, "textScale", loaded.textScale);
            loaded.theme                    = jsonString(json, "theme", loaded.theme);
            loaded.customThemeBackground    = jsonVec4(json, "customThemeBackground", loaded.customThemeBackground);
            loaded.customThemePanel         = jsonVec4(json, "customThemePanel", loaded.customThemePanel);
            loaded.customThemeText          = jsonVec4(json, "customThemeText", loaded.customThemeText);
            loaded.customThemeAccent        = jsonVec4(json, "customThemeAccent", loaded.customThemeAccent);
            loaded.interfaceFont            = jsonString(json, "interfaceFont", loaded.interfaceFont);
            loaded.monospaceFont            = jsonString(json, "monospaceFont", loaded.monospaceFont);
            loaded.interfaceFontSize        = jsonInt(json, "interfaceFontSize", loaded.interfaceFontSize);
            loaded.monospaceFontSize        = jsonInt(json, "monospaceFontSize", loaded.monospaceFontSize);
            loaded.useSystemFonts           = jsonBool(json, "useSystemFonts", loaded.useSystemFonts);
            loaded.showSplashOnStartup      = jsonBool(json, "showSplashOnStartup", loaded.showSplashOnStartup);
            loaded.enableAnimations         = jsonBool(json, "enableAnimations", loaded.enableAnimations);
            loaded.externalEditor           = jsonString(json, "externalEditor", loaded.externalEditor);
            loaded.enableAgent              = jsonBool(json, "enableAgent", loaded.enableAgent);
            loaded.autoStartMcp             = jsonBool(json, "autoStartMcp", loaded.autoStartMcp);
            loaded.mcpServerName            = jsonString(json, "mcpServerName", loaded.mcpServerName);
            loaded.mcpCommand               = jsonString(json, "mcpCommand", loaded.mcpCommand);
            loaded.mcpArguments             = jsonString(json, "mcpArguments", loaded.mcpArguments);
            loaded.agentEndpoint            = jsonString(json, "agentEndpoint", loaded.agentEndpoint);
            loaded.agentModel               = jsonString(json, "agentModel", loaded.agentModel);
            loaded.allowAgentEngineOperations =
                jsonBool(json, "allowAgentEngineOperations", loaded.allowAgentEngineOperations);
            loaded.allowAgentProjectOperations =
                jsonBool(json, "allowAgentProjectOperations", loaded.allowAgentProjectOperations);
            loaded.requireAgentConfirmation =
                jsonBool(json, "requireAgentConfirmation", loaded.requireAgentConfirmation);
            normalizeEditorSettings(loaded);
            settings = std::move(loaded);
            return true;
        }
        catch (const std::exception& e)
        {
            if (error)
                *error = std::string {"failed to parse editor settings: "} + e.what();
            return false;
        }
    }

    bool saveEditorSettings(const std::filesystem::path&        path,
                            const AppState::EditorSettings&    settings,
                            std::string*                       error)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec)
        {
            if (error)
                *error = "failed to create editor settings folder: " + ec.message();
            return false;
        }

        auto normalized = settings;
        normalizeEditorSettings(normalized);

        const nlohmann::json json {
            {"version", 1},
            {"applicationScale", normalized.applicationScale},
            {"textScale", normalized.textScale},
            {"theme", normalized.theme},
            {"customThemeBackground", jsonVec4Value(normalized.customThemeBackground)},
            {"customThemePanel", jsonVec4Value(normalized.customThemePanel)},
            {"customThemeText", jsonVec4Value(normalized.customThemeText)},
            {"customThemeAccent", jsonVec4Value(normalized.customThemeAccent)},
            {"interfaceFont", normalized.interfaceFont},
            {"monospaceFont", normalized.monospaceFont},
            {"interfaceFontSize", normalized.interfaceFontSize},
            {"monospaceFontSize", normalized.monospaceFontSize},
            {"useSystemFonts", normalized.useSystemFonts},
            {"showSplashOnStartup", normalized.showSplashOnStartup},
            {"enableAnimations", normalized.enableAnimations},
            {"externalEditor", normalized.externalEditor},
            {"enableAgent", normalized.enableAgent},
            {"autoStartMcp", normalized.autoStartMcp},
            {"mcpServerName", normalized.mcpServerName},
            {"mcpCommand", normalized.mcpCommand},
            {"mcpArguments", normalized.mcpArguments},
            {"agentEndpoint", normalized.agentEndpoint},
            {"agentModel", normalized.agentModel},
            {"allowAgentEngineOperations", normalized.allowAgentEngineOperations},
            {"allowAgentProjectOperations", normalized.allowAgentProjectOperations},
            {"requireAgentConfirmation", normalized.requireAgentConfirmation},
        };

        std::ofstream file(path, std::ios::trunc);
        if (!file)
        {
            if (error)
                *error = "failed to write editor settings: " + path.generic_string();
            return false;
        }

        file << json.dump(4) << '\n';
        return true;
    }
} // namespace vultra_app
