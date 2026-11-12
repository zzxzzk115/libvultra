-- Shared by the scripts that package an Android application: where the SDK's tools are and what an APK holds.

-- The numbers of a name such as "36.0.0" or "android-37.0".
local function version_of(name)
    local numbers = {}
    for number in name:gmatch("%d+") do
        table.insert(numbers, tonumber(number))
    end
    return numbers
end

local function newer(a, b)
    for index = 1, math.max(#a, #b) do
        if (a[index] or 0) ~= (b[index] or 0) then
            return (a[index] or 0) > (b[index] or 0)
        end
    end
    return false
end

-- The directory below `directory` with the highest version in its name.
local function newest(directory)
    local found
    for _, candidate in ipairs(os.dirs(path.join(directory, "*"))) do
        local version = version_of(path.filename(candidate))
        if #version > 0 and (not found or newer(version, found.version)) then
            found = {directory = candidate, version = version}
        end
    end
    return found and found.directory
end

-- The build tools of an SDK: the newest installed version. A tool is a program, a script or a batch file.
function build_tools(sdk)
    local directory = newest(path.join(sdk, "build-tools"))
    if not directory then
        raise("No build tools are installed in " .. sdk)
    end
    return function(name)
        for _, suffix in ipairs(is_host("windows") and {".exe", ".bat"} or {""}) do
            local tool = path.join(directory, name .. suffix)
            if os.isfile(tool) then
                return tool
            end
        end
        raise("Android build tool is missing: " .. path.join(directory, name))
    end
end

-- android.jar of the newest installed platform.
function platform_jar(sdk)
    local directory = newest(path.join(sdk, "platforms"))
    local jar = directory and path.join(directory, "android.jar")
    if not jar or not os.isfile(jar) then
        raise("No Android platform is installed in " .. sdk)
    end
    return jar
end

-- Checks the signature and the alignment of an APK and returns its entries: name -> true when the entry is
-- stored without compression.
function verify(apk, tool)
    os.vrunv(tool("apksigner"), {"verify", "--verbose", apk})
    local listing = os.iorunv(tool("zipalign"), {"-P", "16", "-c", "-v", "4", apk})
    local entries = {}
    for name, state in listing:gmatch("%d+ ([^\r\n]-) %((OK[^%)]*)%)") do
        entries[name] = not state:find("compressed", 1, true)
    end
    return entries
end

function main()
end
