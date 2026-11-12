-- Packages the built platform probe into a signed APK. The SDK's build tools and a JDK (keytool, java) on PATH
-- are required.
--
--     xmake lua tests/platform/package_android.lua <sdk> <abi> <library> <vpk> <output>
--                                                  [--manifest FILE] [--library-name NAME]
--
-- `output` is a fresh directory; it receives the APK and a signing key made for this test only.
import("android_sdk", {rootdir = path.join(os.scriptdir(), "..", "..", "scripts")})

function main(...)
    local positional = {}
    local options = {manifest = path.join(os.scriptdir(), "android_manifest.xml"), ["library-name"] = "platform-render"}
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
    if #positional ~= 5 then
        raise("usage: xmake lua tests/platform/package_android.lua <sdk> <abi> <library> <vpk> <output> " ..
                  "[--manifest FILE] [--library-name NAME]")
    end
    local sdk, abi, library, vpk, output = table.unpack(positional)
    if abi ~= "arm64-v8a" and abi ~= "x86_64" then
        raise("abi must be arm64-v8a or x86_64")
    end
    for _, file in ipairs({library, vpk, options.manifest}) do
        if not os.isfile(file) then
            raise("required file is missing: " .. file)
        end
    end
    if os.exists(output) then
        raise("output must be a fresh directory: " .. output)
    end
    sdk, output = path.absolute(sdk), path.absolute(output)
    local tool = android_sdk.build_tools(sdk)

    -- The pack is an asset that the application maps directly, so it must stay uncompressed.
    local assets = path.join(output, "assets")
    os.mkdir(assets)
    os.cp(vpk, path.join(assets, "project.vpk"))
    local base = path.join(output, "base.apk")
    os.vrunv(tool("aapt2"), {"link", "-I", android_sdk.platform_jar(sdk), "--manifest", path.absolute(options.manifest),
                             "-A", assets, "-0", "vpk", "-o", base})
    -- The library is added under the name the loader looks for, uncompressed so it can be loaded from the APK.
    local name = "lib/" .. abi .. "/lib" .. options["library-name"] .. ".so"
    local staging = path.join(output, "staging")
    os.cp(library, path.join(staging, name))
    os.vrunv(tool("aapt"), {"add", "-0", "so", base, name}, {curdir = staging})

    local aligned = path.join(output, "aligned.apk")
    os.vrunv(tool("zipalign"), {"-P", "16", "-f", "4", base, aligned})
    local key = path.join(output, "test.jks")
    os.vrunv("keytool", {"-genkeypair", "-keystore", key, "-storepass", "android", "-keypass", "android", "-alias",
                         "test", "-keyalg", "RSA", "-validity", "30", "-dname", "CN=Vultra Platform Test", "-noprompt"})
    local apk = path.join(output, "platform-probe.apk")
    os.vrunv(tool("apksigner"), {"sign", "--ks", key, "--ks-pass", "pass:android", "--out", apk, aligned})
    local entries = android_sdk.verify(apk, tool)
    if not entries["assets/project.vpk"] then
        raise("APK compressed the VPK or lost it; direct mapping requires stored bytes")
    end
    if entries[name] == nil then
        raise("APK does not contain " .. name)
    end
    print(apk)
end
