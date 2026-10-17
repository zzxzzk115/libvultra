#include <vultra/assets/project_manifest.hpp>
#include <vultra/scene/render_nodes.hpp>
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
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void lifecycleParity(const std::filesystem::path& binaryDirectory)
    {
        using namespace vultra;
        const auto name  = "lifecycle-" + StableId::generate().toString();
        const auto trace = std::filesystem::path("build/.tmp") / (name + ".txt");
        const auto lua   = std::filesystem::path("build/.tmp") / (name + ".lua");
        SceneTree  scene(std::make_unique<Node>(name));
        auto&      native  = scene.addChild(scene.root(), std::make_unique<Node3D>("Native"));
        auto&      luaNode = scene.addChild(native, std::make_unique<Node3D>("Lua"));
        auto&      managed = scene.addChild(luaNode, std::make_unique<Node3D>("Managed"));
        std::ofstream(lua) << R"lua(
local script = {}
local function record(self, phase)
    local file = assert(io.open('build/.tmp/' .. self.scene:root():name() .. '.txt', 'a'))
    file:write(self.node:name() .. ':' .. phase .. '\n')
    file:close()
end
function script:_enter_tree() record(self, 'enter') end
function script:_ready()
    local resource = self.scene:create_resource("Resource")
    assert(resource:get_class() == "Resource")
    resource:remove()
    local material = self.scene:create_material("Before")
    material:set_property("name", "After")
    assert(material:get_class() == "MaterialResource" and material:get_property("name") == "After")
    material:remove()
    local x, y, z = self.node:position()
    self.node:set_position(x + 1, y, z)
    self.node:set_process_priority(-7)
    self.scene:set_time_scale(self.scene:time_scale())
    assert(self.scene:time_scale() == 1)
    assert(self.node:is_inside_tree() and self.node:get_process_priority() == -7)
    record(self, 'ready')
end
function script:_physics_process(delta)
    assert(math.abs(delta - 1 / 60) < 1e-7 and self.scene:physics_frame() > 0)
    local action = self.scene:action("jump")
    assert(action.held and action.pressed == (self.scene:physics_frame() == 1))
    local x, y, z = self.node:position()
    self.node:set_position(x, y + 1, z)
    record(self, 'fixed')
end
function script:_process(delta)
    local x, y, z = self.node:position()
    self.node:set_position(x, y, z + 1)
    record(self, 'process')
end
function script:_exit_tree()
    assert(self.node:is_inside_tree())
    record(self, 'exit')
end
return script
)lua";
#ifdef _WIN32
        const auto nativeModule = binaryDirectory / "test-native-lifecycle-script.dll";
#else
        const auto nativeModule = binaryDirectory / "libtest-native-lifecycle-script.so";
#endif
        scene.input().bind("jump", KeyCode::eSpace);
        Input replayInput;
        replayInput.setKey(KeyCode::eSpace, InputAction::ePress);
        replayInput.advanceFrame();
        scene.input().update(replayInput.events());
        ScriptHost host(scene);
        // Deliberately attach in reverse tree order: the tree, not the language/registration order, dispatches.
        host.add({ScriptModule::Language::eCSharp, {}, "VultraTests.LifecycleProbe", managed.idInScene().value},
                 "build/.tmp/scripting-managed/Vultra.SceneScripts.dll");
        host.add({ScriptModule::Language::eLua, {}, {}, luaNode.idInScene().value}, lua);
        host.add({ScriptModule::Language::eNative, {}, "LifecycleProbe", native.idInScene().value}, nativeModule);
        host.update(static_cast<float>(scene.fixedDelta()));
        for (const auto* node : {&native, &luaNode, &managed})
        {
            require(node->localTransform()[3] == glm::vec4(1, 1, 1, 1), "Script lifecycle counters differ by language");
        }
        scene.setPaused(true);
        host.update(static_cast<float>(scene.fixedDelta()));
        for (const auto* node : {&native, &luaNode, &managed})
        {
            require(node->localTransform()[3] == glm::vec4(1, 1, 1, 1), "Paused scripts were still processed");
        }
        scene.stepPaused();
        for (const auto* node : {&native, &luaNode, &managed})
        {
            require(node->localTransform()[3] == glm::vec4(1, 2, 1, 1), "Single-step did not reach every language");
        }
        host.stop();
        std::ifstream            input(trace);
        std::vector<std::string> lines;
        for (std::string line; std::getline(input, line);)
        {
            lines.push_back(line);
        }
        require(lines == std::vector<std::string> {"Native:enter",
                                                   "Lua:enter",
                                                   "Managed:enter",
                                                   "Managed:ready",
                                                   "Lua:ready",
                                                   "Native:ready",
                                                   "Native:fixed",
                                                   "Lua:fixed",
                                                   "Managed:fixed",
                                                   "Native:process",
                                                   "Lua:process",
                                                   "Managed:process",
                                                   "Native:fixed",
                                                   "Lua:fixed",
                                                   "Managed:fixed",
                                                   "Managed:exit",
                                                   "Lua:exit",
                                                   "Native:exit"},
                "Native/Lua/C# lifecycle does not follow a shared tree/clock/order");
    }
} // namespace

int main(int argc, char** argv)
{
    require(argc > 0, "Missing executable path");
    lifecycleParity(std::filesystem::absolute(argv[0]).parent_path());
    {
        const auto propertiesPath = std::filesystem::path("build/.tmp/skeleton-properties.lua");
        std::ofstream(propertiesPath) << R"(return {
            _ready = function(self)
                assert(self.node:get_property("name") == self.node:name())
                self.node:set_property("processPriority", 12)
                assert(self.node:get_property("processPriority") == 12)
                local transform = self.node:get_property("localTransform")
                transform[13] = 3
                self.node:set_property("localTransform", transform)
                local ok = pcall(function() self.node:set_property("processPriority", false) end)
                assert(not ok and self.node:get_property("processPriority") == 12)
                local bad = pcall(function() self.node:set_property("localTransform", { bad = 4 }) end)
                assert(not bad and self.node:get_property("localTransform")[13] == 3)
            end
        })";
        vultra::SceneTree  propertyScene(std::make_unique<vultra::Node3D>("Properties"));
        vultra::ScriptHost propertyHost(propertyScene);
        propertyHost.add({vultra::ScriptModule::Language::eLua, {}}, propertiesPath);
        propertyHost.update(0);
        require(propertyScene.root().processPriority() == 12 &&
                    static_cast<vultra::Node3D&>(propertyScene.root()).localTransform()[3].x == 3,
                "Lua generic properties failed to update validated ECS values");
    }

    auto  scene  = vultra::SceneTree(std::make_unique<vultra::Node>("Root"));
    auto& ship   = scene.addChild(scene.root(), std::make_unique<vultra::Node3D>("Ship"));
    auto& beacon = scene.addChild(scene.root(), std::make_unique<vultra::Node3D>("Beacon"));
    beacon.setLocalTransform(glm::translate(glm::mat4(1), {0.68f * std::cos(0.6f), 0.68f * std::sin(0.6f), 0}));
    auto& throttle = scene.addChild(
        scene.root(),
        std::make_unique<vultra::MeshInstance3D>("Throttle", vultra::AssetId {vultra::StableId::generate()}));
    throttle.setLocalTransform(glm::translate(glm::mat4(1), {0, -0.92f, 0}));
    const auto root = std::filesystem::current_path();
#ifdef _WIN32
    const auto multiClass = std::filesystem::absolute(argv[0]).parent_path() / "test-native-script-classes.dll";
#else
    const auto multiClass = std::filesystem::absolute(argv[0]).parent_path() / "libtest-native-script-classes.so";
#endif
    {
        vultra::SceneTree  classScene(std::make_unique<vultra::Node>("Root"));
        auto&              first  = classScene.addChild(classScene.root(), std::make_unique<vultra::Node3D>("First"));
        auto&              second = classScene.addChild(classScene.root(), std::make_unique<vultra::Node3D>("Second"));
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
                                                    std::make_unique<vultra::MeshInstance3D>("First", firstModel));
        auto& secondMesh = structuralScene.addChild(structuralScene.root(),
                                                    std::make_unique<vultra::MeshInstance3D>("Second", secondModel));
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
                    static_cast<const vultra::MeshInstance3D&>(*structuralScene.root().children()[2]).model() ==
                        firstModel &&
                    static_cast<const vultra::MeshInstance3D&>(secondMesh).model() == selectedModel &&
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
               "local group = root:create_child_3d('Group'); "
               "group:set_position(1, 2, 0); "
               "local failed = pcall(function() root:create_mesh_child('Bad', 'bad') end); "
               "assert(not failed); "
               "local mesh = root:create_mesh_child('Created', '"
            << model.value.toString()
            << "'); mesh:set_position(0, 0, 3); mesh:reparent(group, false) "
               "end, _process = function(self, dt) end }\n";
        vultra::ScriptHost compositionHost(composed, false, &project);
        compositionHost.add({vultra::ScriptModule::Language::eLua, {}}, compositionScript);
        compositionHost.update(0.01f);
        require(composed.root().children().size() == 1 && composed.root().children()[0]->children().size() == 1,
                "Lua scene composition left an invalid or misplaced node");
        const auto& group = *composed.root().children()[0];
        const auto& mesh  = static_cast<const vultra::MeshInstance3D&>(*group.children()[0]);
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

    {
        const auto        identityName = "identity-world-" + vultra::StableId::generate().toString();
        const auto        exitMarker   = root / "build/.tmp" / (identityName + "-exits.txt");
        vultra::SceneTree identityScene(std::make_unique<vultra::Node>(identityName));
        auto&      first     = identityScene.addChild(identityScene.root(), std::make_unique<vultra::Node3D>("First"));
        auto&      second    = identityScene.addChild(identityScene.root(), std::make_unique<vultra::Node3D>("Second"));
        const auto source    = root / "build/.tmp/scripting-managed/Vultra.SceneScripts.dll";
        const auto directory = root / "build/.tmp" / ("identity-reload-" + vultra::StableId::generate().toString());
        std::filesystem::create_directories(directory);
        const auto assembly = directory / source.filename();
        std::filesystem::copy_file(source, assembly);
        for (const auto* sidecar :
             {"Vultra.ManagedHost.dll", "Vultra.Scripting.dll", "Vultra.ManagedHost.runtimeconfig.json"})
        {
            std::filesystem::copy_file(source.parent_path() / sidecar, directory / sidecar);
        }
        vultra::ScriptHost identities(identityScene, true);
        identities.add(
            {vultra::ScriptModule::Language::eCSharp, {}, "VultraTests.IdentityProbe", first.idInScene().value},
            assembly);
        identities.add(
            {vultra::ScriptModule::Language::eCSharp, {}, "VultraTests.IdentityProbe", second.idInScene().value},
            assembly);
        bool duplicateAttachmentRejected = false;
        try
        {
            identities.add(
                {vultra::ScriptModule::Language::eCSharp, {}, "VultraTests.IdentityProbe", first.idInScene().value},
                assembly);
        }
        catch (const std::invalid_argument&)
        {
            duplicateAttachmentRejected = true;
        }
        require(duplicateAttachmentRejected, "A node accepted two script attachments");
        identities.update(0.25f);
        require(first.localTransform()[3].x == 1 && second.localTransform()[3].x == 2,
                "Managed scripts do not share canonical scene/node identities");
        std::ofstream(assembly, std::ios::binary | std::ios::trunc) << "invalid managed assembly";
        require(identities.reloadChanged() == 0 && !identities.lastReloadError().empty(),
                "Invalid managed cohort replacement was accepted");
        identities.update(0.25f);
        require(first.localTransform()[3].x == 1 && second.localTransform()[3].x == 2,
                "Failed managed replacement damaged active identities");
        std::filesystem::copy_file(root / "build/.tmp/scripting-managed-identity-replacement/Vultra.SceneScripts.dll",
                                   assembly,
                                   std::filesystem::copy_options::overwrite_existing);
        require(identities.reloadChanged() == 2 && identities.lastReloadError().empty(),
                "Valid managed module did not replace both instances");
        identities.update(0.25f);
        require(first.localTransform()[3].x == 3 && second.localTransform()[3].x == 4,
                "Reloaded managed instances lost shared type/node identity");
        require(!std::filesystem::exists(exitMarker), "Managed reload incorrectly exited the live scene nodes");
        identities.stop();
        require(std::filesystem::file_size(exitMarker) == 2, "Managed nodes did not exit exactly once at stop");
        bool wrongBaseRejected = false;
        try
        {
            vultra::ScriptHost invalid(identityScene);
            invalid.add({vultra::ScriptModule::Language::eCSharp,
                         {},
                         "VultraTests.IdentityProbe",
                         identityScene.root().idInScene().value},
                        assembly);
        }
        catch (const std::runtime_error&)
        {
            wrongBaseRejected = true;
        }
        require(wrongBaseRejected, "A Node3D script was attached to a non-spatial Node");
    }

    const auto scratch =
        root / "build/.tmp" /
        ("script-reload-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(scratch);
    const auto classCopy = scratch / multiClass.filename();
    std::filesystem::copy_file(multiClass, classCopy);
    {
        vultra::SceneTree  classScene(std::make_unique<vultra::Node>("Root"));
        auto&              first  = classScene.addChild(classScene.root(), std::make_unique<vultra::Node3D>("First"));
        auto&              second = classScene.addChild(classScene.root(), std::make_unique<vultra::Node3D>("Second"));
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
