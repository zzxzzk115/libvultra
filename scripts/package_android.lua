-- Packages a built Vultra shared library and its VPK with GameActivity into a debug-signed APK. It builds
-- nothing itself: the library and the pack are already built. The Android SDK, Gradle and a JDK (keytool) on
-- PATH are required.
--
--     xmake lua scripts/package_android.lua <library> <vpk> <output> --sdk DIR --gradle FILE --abi ABI
--                                           [--application-id ID] [--manifest FILE]
--
-- `output` is a fresh directory; it receives the APK and the Gradle project it was built from.
import("core.base.xml")
import("android_sdk", {rootdir = os.scriptdir()})

-- The manifest with the library to load filled in. Gradle supplies the package name, so it is dropped.
local function manifest_for(file, library)
    local document = xml.loadfile(file)
    local activity = xml.find(document, "//application/activity")
    if not activity or activity.attrs["android:name"] ~= "com.google.androidgamesdk.GameActivity" then
        raise("manifest must launch GameActivity")
    end
    local declared = false
    for _, child in ipairs(activity.children or {}) do
        if child.name == "meta-data" and child.attrs["android:name"] == "android.app.lib_name" then
            child.attrs["android:value"] = library
            declared = true
        end
    end
    if not declared then
        raise("manifest must declare android.app.lib_name")
    end
    local root = xml.find(document, "/manifest")
    root.attrs.package = nil
    return document
end

function main(...)
    local positional = {}
    local options = {sdk = false, gradle = false, abi = false, ["application-id"] = "org.vultra.example",
                     manifest = path.join(os.scriptdir(), "..", "examples", "common", "android_manifest.xml")}
    local arguments = {...}
    local index = 1
    while index <= #arguments do
        local name = arguments[index]:match("^%-%-(.+)$")
        if name then
            if options[name] == nil or not arguments[index + 1] then
                raise("Unknown or incomplete option: " .. arguments[index])
            end
            options[name] = arguments[index + 1]
            index = index + 2
        else
            table.insert(positional, arguments[index])
            index = index + 1
        end
    end
    if #positional ~= 3 or not options.sdk or not options.gradle or not options.abi then
        raise("usage: xmake lua scripts/package_android.lua <library> <vpk> <output> --sdk DIR --gradle FILE " ..
                  "--abi ABI [--application-id ID] [--manifest FILE]")
    end
    if options.abi ~= "arm64-v8a" and options.abi ~= "x86_64" then
        raise("abi must be arm64-v8a or x86_64")
    end
    local library, vpk, output = table.unpack(positional)
    local name = path.filename(library):match("^lib(.+)%.so$")
    if not name then
        raise("library must be a built libNAME.so")
    end
    for _, file in ipairs({library, vpk, options.manifest, options.gradle}) do
        if not os.isfile(file) then
            raise("required file is missing: " .. file)
        end
    end
    local sdk = path.absolute(options.sdk)
    local tool = android_sdk.build_tools(sdk)
    tool("apksigner")
    tool("zipalign")
    local manifest = manifest_for(options.manifest, name)

    -- Inputs are checked before the fresh output directory is created; a failed build stays for diagnosis.
    if os.exists(output) then
        raise("output must be a fresh directory: " .. output)
    end
    output = path.absolute(output)
    local project = path.join(output, "project")
    os.mkdir(output)
    os.cp(path.join(os.scriptdir(), "..", "source", "platform", "android"), project)
    xml.savefile(path.join(project, "AndroidManifest.xml"), manifest)
    os.cp(library, path.join(project, "jniLibs", options.abi, path.filename(library)))
    os.cp(vpk, path.join(project, "assets", "project.vpk"))
    io.writefile(path.join(project, "local.properties"), "sdk.dir=" .. sdk:gsub("\\", "/") .. "\n")
    -- The debug key belongs to this package; callers can isolate Gradle downloads with GRADLE_USER_HOME.
    os.vrunv("keytool", {"-genkeypair", "-keystore", path.join(project, "debug.keystore"), "-storepass", "android",
                         "-keypass", "android", "-alias", "test", "-keyalg", "RSA", "-validity", "30", "-dname",
                         "CN=Vultra Android Example", "-noprompt"})
    os.vrunv(path.absolute(options.gradle), {"--no-daemon", "--project-dir", project,
                                             "-PvultraApplicationId=" .. options["application-id"], "assembleDebug"})
    local apks = os.files(path.join(project, "build", "outputs", "apk", "debug", "*.apk"))
    if #apks ~= 1 then
        raise("Expected exactly one debug APK")
    end
    local apk = path.join(output, name .. ".apk")
    os.cp(apks[1], apk)

    local entries = android_sdk.verify(apk, tool)
    if not entries["assets/project.vpk"] then
        raise("APK compressed its embedded VPK or lost it")
    end
    if entries["classes.dex"] == nil then
        raise("GameActivity Java classes are missing")
    end
    for entry in pairs(entries) do
        if entry:endswith("libc++_shared.so") then
            raise("Base Android application must statically link the C++ runtime")
        end
    end
    print(apk)
end
