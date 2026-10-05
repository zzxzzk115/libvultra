#include <vultra/assets/project_manifest.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main(int argc, char** argv)
{
    require(argc > 0, "Missing executable path");
    auto  scene  = vultra::SceneTree(std::make_unique<vultra::Node>("Root"));
    auto& ship   = scene.addChild(scene.root(), std::make_unique<vultra::Node>("Ship"));
    auto& beacon = scene.addChild(scene.root(), std::make_unique<vultra::Node>("Beacon"));
    beacon.setLocalTransform(glm::translate(glm::mat4(1), {0.68f * std::cos(0.6f), 0.68f * std::sin(0.6f), 0}));
    auto& throttle = scene.addChild(scene.root(), std::make_unique<vultra::Node>("Throttle"));
    throttle.setLocalTransform(glm::translate(glm::mat4(1), {0, -0.92f, 0}));
    const auto root = std::filesystem::current_path();
#ifdef _WIN32
    const auto multiClass = std::filesystem::absolute(argv[0]).parent_path() / "test-native-script-classes.dll";
#else
    const auto multiClass = std::filesystem::absolute(argv[0]).parent_path() / "libtest-native-script-classes.so";
#endif
    {
        vultra::SceneTree  classScene(std::make_unique<vultra::Node>("Root"));
        auto&              first  = classScene.addChild(classScene.root(), std::make_unique<vultra::Node>("First"));
        auto&              second = classScene.addChild(classScene.root(), std::make_unique<vultra::Node>("Second"));
        vultra::ScriptHost classes(classScene);
        classes.addExtension(multiClass);
        classes.add({vultra::ScriptModule::Language::eNative, {}, "First", first.idInScene().value}, multiClass);
        classes.add({vultra::ScriptModule::Language::eNative, {}, "Second", second.idInScene().value}, multiClass);
        classes.update(0.25f);
        require(first.localTransform()[3].x == 1.0f && second.localTransform()[3].x == 2.0f,
                "Native module did not instantiate its registered classes on separate nodes");
        classes.stop();
        bool unknownClassRejected = false;
        try
        {
            vultra::ScriptHost invalidClass(classScene);
            invalidClass.addExtension(multiClass);
            invalidClass.add({vultra::ScriptModule::Language::eNative, {}, "Missing", first.idInScene().value},
                             multiClass);
        }
        catch (const std::invalid_argument&)
        {
            unknownClassRejected = true;
        }
        require(unknownClassRejected, "Native module accepted an unregistered script class");
    }
#ifdef _WIN32
    const auto native    = std::filesystem::absolute(argv[0]).parent_path() / "example-native-plugin.dll";
    const auto nativeCpp = std::filesystem::absolute(argv[0]).parent_path() / "example-native-cpp-plugin.dll";
#else
    const auto native    = std::filesystem::absolute(argv[0]).parent_path() / "libexample-native-plugin.so";
    const auto nativeCpp = std::filesystem::absolute(argv[0]).parent_path() / "libexample-native-cpp-plugin.so";
#endif
    vultra::ScriptHost host(scene);
    host.addExtension(native);
    host.add({vultra::ScriptModule::Language::eNative, {}, "ShipOrbit", ship.idInScene().value}, nativeCpp);
    host.add({vultra::ScriptModule::Language::eLua, {}, {}, beacon.idInScene().value},
             root / "examples/scripting/lua/scene_probe.lua");
    if (argc > 1)
    {
        host.add({vultra::ScriptModule::Language::eCSharp,
                  {},
                  "VultraScript.ThrottleController",
                  throttle.idInScene().value},
                 argv[1]);
    }
    host.update(0.25f);
    require(ship.localTransform()[3].y > 0, "Native script did not move the rendered ship");
    require(beacon.localTransform()[3].x < 0, "Lua pickup rule did not relocate the beacon");
    host.stop();
    host.stop();
    bool missingNodeRejected = false;
    try
    {
        vultra::ScriptHost missingNode(scene);
        missingNode.add({vultra::ScriptModule::Language::eLua, {}, {}, vultra::StableId::generate()},
                        root / "examples/scripting/lua/scene_probe.lua");
    }
    catch (const std::invalid_argument&)
    {
        missingNodeRejected = true;
    }
    require(missingNodeRejected, "Script host accepted a missing target node");
    bool stoppedRejected = false;
    try
    {
        host.update(0.25f);
    }
    catch (const std::logic_error&)
    {
        stoppedRejected = true;
    }
    require(stoppedRejected, "Stopped script host accepted update");

    const auto structuralScript = root / "build/.tmp/script-host-structure.lua";
    std::filesystem::create_directories(structuralScript.parent_path());
    {
        vultra::SceneTree       structuralScene(std::make_unique<vultra::Node>("Root"));
        const vultra::AssetId   firstModel {vultra::StableId::generate()};
        const vultra::AssetId   secondModel {vultra::StableId::generate()};
        vultra::ProjectManifest project;
        const auto              selectedModel = project.addAsset("models/selected.glb");
        auto& firstMesh  = structuralScene.addChild(structuralScene.root(),
                                                    std::make_unique<vultra::MeshInstanceNode>("First", firstModel));
        auto& secondMesh = structuralScene.addChild(structuralScene.root(),
                                                    std::make_unique<vultra::MeshInstanceNode>("Second", secondModel));
        std::ofstream(structuralScript) << "return { _ready = function(self) "
                                           "local root = self.scene:root(); "
                                           "self.copy = root:get_child(0):duplicate_mesh(root); "
                                           "root:get_child(1):copy_mesh_model(self.copy); "
                                           "local ok = pcall(function() root:get_child(1):set_mesh_model('bad') end); "
                                           "assert(not ok); "
                                           "root:get_child(1):set_mesh_model('"
                                        << selectedModel.value.toString()
                                        << "') end, _process = function(self, dt) end }\n";
        vultra::ScriptHost structuralHost(structuralScene, false, &project);
        structuralHost.add({vultra::ScriptModule::Language::eLua, {}}, structuralScript);
        structuralHost.update(0.01f);
        require(structuralScene.root().children().size() == 3 &&
                    static_cast<const vultra::MeshInstanceNode&>(*structuralScene.root().children()[2]).model() ==
                        firstModel &&
                    static_cast<const vultra::MeshInstanceNode&>(secondMesh).model() == selectedModel &&
                    firstMesh.idInScene() != structuralScene.root().children()[2]->idInScene(),
                "Lua scene edits did not duplicate a mesh and replace its model");
        structuralHost.stop();
    }

    const auto compositionScript = root / "build/.tmp/script-host-compose.lua";
    {
        vultra::ProjectManifest project;
        const auto              model = project.addAsset("models/created.obj");
        vultra::SceneTree       composed(std::make_unique<vultra::Node>("Root"));
        std::ofstream(compositionScript)
            << "return { _ready = function(self) "
               "local root = self.scene:root(); "
               "local group = root:create_child('Group'); "
               "group:set_position(1, 2, 0); "
               "local failed = pcall(function() root:create_mesh_child('Bad', 'bad') end); "
               "assert(not failed); "
               "local mesh = root:create_mesh_child('Created', '"
            << model.value.toString()
            << "'); mesh:set_position(0, 0, 3); mesh:reparent(group) "
               "end, _process = function(self, dt) end }\n";
        vultra::ScriptHost compositionHost(composed, false, &project);
        compositionHost.add({vultra::ScriptModule::Language::eLua, {}}, compositionScript);
        compositionHost.update(0.01f);
        require(composed.root().children().size() == 1 && composed.root().children()[0]->children().size() == 1,
                "Lua scene composition left an invalid or misplaced node");
        const auto& group = *composed.root().children()[0];
        const auto& mesh  = static_cast<const vultra::MeshInstanceNode&>(*group.children()[0]);
        require(mesh.model() == model && mesh.localTransform()[3].z == 3 && mesh.globalTransform()[3].x == 1 &&
                    mesh.globalTransform()[3].y == 2,
                "Lua scene composition lost model selection or parent transforms");
        compositionHost.stop();
    }

    const auto removalScript = root / "build/.tmp/script-host-remove.lua";
    {
        vultra::SceneTree removalScene(std::make_unique<vultra::Node>("Root"));
        auto&             group  = removalScene.addChild(removalScene.root(), std::make_unique<vultra::Node>("Group"));
        auto&             target = removalScene.addChild(group, std::make_unique<vultra::Node>("Target"));
        const auto        removedId = target.idInScene();
        std::ofstream(removalScript) << "return { _process = function(self, dt) "
                                        "self.tick = (self.tick or 0) + 1; "
                                        "if self.tick == 2 then self.scene:root():get_child(0):remove() end end }\n";
        const auto childScript = root / "build/.tmp/script-host-removed-child.lua";
        const auto exitMarker  = root / "build/.tmp/script-host-removed-child.exit";
        std::filesystem::remove(exitMarker);
        std::ofstream(childScript) << "return { _process = function(self, dt) "
                                      "self.tick = (self.tick or 0) + 1; "
                                      "if self.tick > 1 then error('removed child was updated') end end, "
                                      "_exit_tree = function(self) "
                                      "local file = assert(io.open('build/.tmp/script-host-removed-child.exit', 'a')); "
                                      "file:write('x'); file:close() end }\n";
        vultra::ScriptHost removalHost(removalScene, true);
        removalHost.add({vultra::ScriptModule::Language::eLua, {}}, removalScript);
        removalHost.add({vultra::ScriptModule::Language::eLua, {}, {}, removedId.value}, childScript);
        removalHost.update(0.01f);
        require(removalScene.find(removedId), "Lua removed the child before its first update");
        removalHost.update(0.01f);
        require(!removalScene.find(removedId), "Lua did not remove the scripted subtree");
        require(std::filesystem::file_size(exitMarker) == 1, "Removed child script did not receive its exit callback");
        require(removalHost.reloadChanged() == 0, "Reload touched a detached script");
        removalHost.stop();
        require(std::filesystem::file_size(exitMarker) == 1, "Removed child script exited twice");
    }
    {
        vultra::SceneTree removalScene(std::make_unique<vultra::Node>("Root"));
        auto&             target = removalScene.addChild(removalScene.root(), std::make_unique<vultra::Node>("Self"));
        const auto        removedId = target.idInScene();
        std::ofstream(removalScript) << "return { _process = function(self, dt) self.node:remove() end }\n";
        vultra::ScriptHost removalHost(removalScene);
        removalHost.add({vultra::ScriptModule::Language::eLua, {}, {}, removedId.value}, removalScript);
        removalHost.update(0.01f);
        require(!removalScene.find(removedId), "Lua script did not remove its own node");
        removalHost.update(0.01f);
        removalHost.stop();
    }

    const auto badScript = root / "build/.tmp/script-host-invalid.lua";
    std::filesystem::create_directories(badScript.parent_path());
    {
        std::ofstream output(badScript);
        output << "return { _process = function(self, dt) self.scene:root():get_child(99) end }\n";
    }
    vultra::ScriptHost bad(scene);
    bad.add({vultra::ScriptModule::Language::eLua, {}}, badScript);
    bool invalidRejected = false;
    try
    {
        bad.update(0.25f);
    }
    catch (const std::runtime_error&)
    {
        invalidRejected = true;
    }
    require(invalidRejected, "Lua scene query accepted a foreign ObjectId");
    bad.stop();

    const auto scratch =
        root / "build/.tmp" /
        ("script-reload-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(scratch);
    const auto classCopy = scratch / multiClass.filename();
    std::filesystem::copy_file(multiClass, classCopy);
    {
        vultra::SceneTree  classScene(std::make_unique<vultra::Node>("Root"));
        auto&              first  = classScene.addChild(classScene.root(), std::make_unique<vultra::Node>("First"));
        auto&              second = classScene.addChild(classScene.root(), std::make_unique<vultra::Node>("Second"));
        vultra::ScriptHost hotClasses(classScene, true);
        hotClasses.addExtension(classCopy);
        hotClasses.add({vultra::ScriptModule::Language::eNative, {}, "First", first.idInScene().value}, classCopy);
        hotClasses.add({vultra::ScriptModule::Language::eNative, {}, "Second", second.idInScene().value}, classCopy);
        hotClasses.update(0.25f);
        bool duplicateRejected = false;
        try
        {
            hotClasses.addExtension(multiClass);
        }
        catch (const std::invalid_argument&)
        {
            duplicateRejected = true;
        }
        require(duplicateRejected, "Host accepted duplicate extension script classes");
#ifdef _WIN32
        const auto badClassModule = std::filesystem::absolute(argv[0]).parent_path() / "test-native-plugin-bad.dll";
#else
        const auto badClassModule = std::filesystem::absolute(argv[0]).parent_path() / "libtest-native-plugin-bad.so";
#endif
        std::filesystem::copy_file(badClassModule, classCopy, std::filesystem::copy_options::overwrite_existing);
        require(hotClasses.reloadChanged() == 0 && !hotClasses.lastReloadError().empty(),
                "Invalid class extension replacement was accepted");
        hotClasses.update(0.25f);
        std::filesystem::copy_file(native, classCopy, std::filesystem::copy_options::overwrite_existing);
        require(hotClasses.reloadChanged() == 0 && !hotClasses.lastReloadError().empty(),
                "Extension replacement without required classes was accepted");
        hotClasses.update(0.25f);
#ifdef _WIN32
        const auto replacementClasses =
            std::filesystem::absolute(argv[0]).parent_path() / "test-native-script-classes-replacement.dll";
#else
        const auto replacementClasses =
            std::filesystem::absolute(argv[0]).parent_path() / "libtest-native-script-classes-replacement.so";
#endif
        std::filesystem::copy_file(replacementClasses, classCopy, std::filesystem::copy_options::overwrite_existing);
        require(hotClasses.reloadChanged() == 1 && hotClasses.lastReloadError().empty(),
                "Class extension and its scripts did not reload together");
        hotClasses.update(0.25f);
        require(first.localTransform()[3].x == 3.0f && second.localTransform()[3].x == 4.0f,
                "Reloaded extension script instances still ran the old code");
        hotClasses.stop();
    }
    const auto nativeCopy = scratch / native.filename();
    const auto luaCopy    = scratch / "script.lua";
    std::filesystem::copy_file(native, nativeCopy);
    std::filesystem::copy_file(root / "examples/scripting/lua/scene_probe.lua", luaCopy);
    vultra::ScriptHost hot(scene, true);
    hot.add({vultra::ScriptModule::Language::eNative, {}, "ShipOrbit", ship.idInScene().value}, nativeCopy);
    hot.add({vultra::ScriptModule::Language::eLua, {}, {}, beacon.idInScene().value}, luaCopy);
    hot.update(0.25f);
    require(hot.reloadChanged() == 0, "Unchanged modules reloaded");
#ifdef _WIN32
    const auto badNative = std::filesystem::absolute(argv[0]).parent_path() / "test-native-plugin-bad.dll";
#else
    const auto badNative = std::filesystem::absolute(argv[0]).parent_path() / "libtest-native-plugin-bad.so";
#endif
    std::filesystem::copy_file(badNative, nativeCopy, std::filesystem::copy_options::overwrite_existing);
    require(hot.reloadChanged() == 0 && !hot.lastReloadError().empty(), "Bad native replacement was accepted");
    require(hot.reloadChanged(true) == 0, "Retry accepted unchanged bad native module");
    hot.update(0.25f);
    {
        std::ofstream output(luaCopy, std::ios::trunc);
        output << "return { _process = function(self, dt) assert(self.scene:root():child_count() == 3) end }\n";
    }
    require(hot.reloadChanged() == 1 && !hot.lastReloadError().empty(),
            "Successful Lua reload hid a different native reload failure");
    std::filesystem::copy_file(nativeCpp, nativeCopy, std::filesystem::copy_options::overwrite_existing);
    require(hot.reloadChanged() == 1 && hot.lastReloadError().empty(),
            "Valid native replacement did not clear its error");
    hot.update(0.25f);
    {
        std::ofstream output(luaCopy, std::ios::trunc);
        output << "return { _process = function(\n";
    }
    require(hot.reloadChanged() == 0 && !hot.lastReloadError().empty(), "Invalid Lua replacement was accepted");
    hot.update(0.25f);
    {
        std::ofstream output(luaCopy, std::ios::trunc);
        output
            << "return { _process = function(self, dt) assert(self.scene:root():get_child(0):name() == 'Ship') end }\n";
    }
    require(hot.reloadChanged() == 1, "Valid Lua replacement was not loaded");
    hot.update(0.25f);
    hot.stop();
    const auto extensionCopy = scratch / "extension.so";
    std::filesystem::copy_file(native, extensionCopy);
    const auto         extensionTimestamp = std::filesystem::last_write_time(extensionCopy);
    vultra::ScriptHost hotExtension(scene, true);
    hotExtension.addExtension(extensionCopy);
    hotExtension.update(0.25f);
    std::filesystem::copy_file(badNative, extensionCopy, std::filesystem::copy_options::overwrite_existing);
    require(hotExtension.reloadChanged() == 0 && !hotExtension.lastReloadError().empty(),
            "Invalid extension replacement was accepted");
    hotExtension.update(0.25f);
    std::filesystem::copy_file(native, extensionCopy, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::last_write_time(extensionCopy, extensionTimestamp);
    require(hotExtension.reloadChanged() == 1 && hotExtension.lastReloadError().empty(),
            "Valid extension replacement was not restored");
    require(hotExtension.reloadChanged() == 0, "Restored extension reloaded without a change");
    hotExtension.stop();
    if (argc > 2)
    {
        const auto managedDirectory = scratch / "managed";
        std::filesystem::create_directories(managedDirectory);
        const auto managedSource = std::filesystem::absolute(argv[1]);
        const auto managedCopy   = managedDirectory / managedSource.filename();
        std::filesystem::copy_file(managedSource, managedCopy);
        for (const auto* sidecar : {"Vultra.ManagedHost.dll",
                                    "Vultra.Scripting.dll",
                                    "Vultra.ManagedHost.runtimeconfig.json",
                                    "Vultra.ManagedHost.deps.json",
                                    "VultraScript.deps.json"})
        {
            const auto source = managedSource.parent_path() / sidecar;
            if (std::filesystem::exists(source))
            {
                std::filesystem::copy_file(source, managedDirectory / sidecar);
            }
        }
        vultra::ScriptHost managed(scene, true);
        managed.add({vultra::ScriptModule::Language::eCSharp,
                     {},
                     "VultraScript.ThrottleController",
                     throttle.idInScene().value},
                    managedCopy);
        managed.update(0.25f);
        {
            vultra::ScriptHost independent(scene, true);
            independent.add({vultra::ScriptModule::Language::eCSharp,
                             {},
                             "VultraScript.ThrottleController",
                             throttle.idInScene().value},
                            managedCopy);
            independent.update(0.25f);
            require(std::abs(throttle.localTransform()[3].x - 0.45f * std::sin(0.25f * 0.7f)) < 0.001f,
                    "New C# instance inherited an unrelated script's state");
        }
        {
            std::ofstream output(managedCopy, std::ios::binary | std::ios::trunc);
            output << "invalid managed image";
        }
        require(managed.reloadChanged() == 0 && !managed.lastReloadError().empty(),
                "Invalid C# replacement was accepted");
        managed.update(0.25f);
        std::filesystem::copy_file(managedSource, managedCopy, std::filesystem::copy_options::overwrite_existing);
        require(managed.reloadChanged() == 1 && managed.lastReloadError().empty(),
                "Original C# module was not restored after an invalid replacement");
        managed.update(0.25f);
        require(std::abs(throttle.localTransform()[3].x - 0.45f * std::sin(0.75f * 0.7f)) < 0.001f,
                "C# reload reset a private script field");
        std::filesystem::copy_file(argv[2], managedCopy, std::filesystem::copy_options::overwrite_existing);
        require(managed.reloadChanged() == 1, "C# replacement was not loaded");
        bool replacementRan = false;
        try
        {
            managed.update(0.25f);
        }
        catch (const std::runtime_error&)
        {
            replacementRan = true;
        }
        require(replacementRan, "Reloaded C# code did not execute");
        std::filesystem::copy_file(managedSource, managedCopy, std::filesystem::copy_options::overwrite_existing);
        require(managed.reloadChanged() == 1, "Original C# module was not restored");
        managed.update(0.25f);
        managed.stop();
    }
    for (const auto& entry : std::filesystem::directory_iterator(scratch))
    {
        require(entry.path().filename().string().find("-vultra-") == std::string::npos,
                "Staged native module remained after stop");
    }
}
