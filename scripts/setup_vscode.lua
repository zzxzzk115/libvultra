import("core.base.json")

-- JSONC permits comments and trailing commas. Keep strings intact before JSON decoding.
function skipTrivia(text, index)
    while index <= #text do
        local pair = text:sub(index, index + 1)
        if text:sub(index, index):match("%s") then
            index = index + 1
        elseif pair == "//" then
            index = text:find("\n", index + 2, true) or (#text + 1)
        elseif pair == "/*" then
            local last = text:find("*/", index + 2, true)
            assert(last, "Unterminated comment in settings.json")
            index = last + 2
        else
            break
        end
    end
    return index
end

function stripJsonc(text)
    text = text:gsub("^\239\187\191", "") -- UTF-8 BOM
    local parts = {}
    local index = 1
    while index <= #text do
        local nextIndex = skipTrivia(text, index)
        if nextIndex ~= index then
            parts[#parts + 1] = " "
            index = nextIndex
        elseif text:sub(index, index) == '"' then
            local last = index + 1
            while last <= #text and text:sub(last, last) ~= '"' do
                if text:sub(last, last) == "\\" then
                    last = last + 1
                end
                last = last + 1
            end
            assert(last <= #text, "Unterminated string in settings.json")
            parts[#parts + 1] = text:sub(index, last)
            index = last + 1
        else
            local char = text:sub(index, index)
            local following = skipTrivia(text, index + 1)
            local nextChar = text:sub(following, following)
            if char ~= "," or (nextChar ~= "}" and nextChar ~= "]") then
                parts[#parts + 1] = char
            end
            index = index + 1
        end
    end
    return table.concat(parts)
end

-- Xmake's pretty encoder uses its Lua null marker, while the decoder may return CJSON's marker.
function prepareForPrettyPrint(value)
    if value == json.null then
        return json.purenull
    end
    if type(value) == "table" then
        for key, item in pairs(value) do
            value[key] = prepareForPrettyPrint(item)
        end
    end
    return value
end

function main(projectDir)
    local root = path.absolute(assert(projectDir, "Pass the Vultra project directory"))
    assert(os.isfile(path.join(root, "xmake.lua")), "Project directory must contain xmake.lua")
    local settingsPath = path.join(root, ".vscode", "settings.json")
    local source = os.isfile(settingsPath) and io.readfile(settingsPath) or "{}"
    local clean = stripJsonc(source)
    assert(clean:match("^%s*{"), "settings.json must contain a JSON object")
    local settings = json.decode(clean)
    local required = {
        ["clangd.arguments"] = {
            "--compile-commands-dir=.vscode",
            "--header-insertion=never",
            "--fallback-style=none"
        },
        ["slang.additionalSearchPaths"] = {
            "${workspaceFolder}/builtin/shaders",
            "${workspaceFolder}/external",
            "${workspaceFolder}/examples/common"
        },
        ["slang.searchInAllWorkspaceDirectories"] = false
    }
    local changed = source:find("\\/", 1, true) ~= nil
    for key, value in pairs(required) do
        if settings[key] == nil or json.encode(settings[key]) ~= json.encode(value) then
            settings[key] = value
            changed = true
        end
    end
    if not changed then
        print("VS Code settings already configured: %s", settingsPath)
        return
    end

    -- Validate and serialize before touching the local file. Preserve the first original as a backup.
    local output = json.encode(prepareForPrettyPrint(settings), {pretty = true}):gsub("\\/", "/") .. "\n"
    os.mkdir(path.directory(settingsPath))
    local backup = settingsPath .. ".bak"
    if os.isfile(settingsPath) and not os.isfile(backup) then
        os.cp(settingsPath, backup)
    end
    local temporary = settingsPath .. ".tmp"
    io.writefile(temporary, output)
    os.mv(temporary, settingsPath)
    print("Configured local VS Code settings: %s", settingsPath)
end
