#include <vultra/api/scene_bridge.hpp>
#include <vultra/scene/render_properties.generated.hpp>
#include <vultra/scene/resource_properties.generated.hpp>
#include <vultra/scene/scene_api.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>

namespace
{
    using namespace vultra;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template<class Callback>
    void requireFailure(Callback&& callback, std::source_location location = std::source_location::current())
    {
        bool rejected = false;
        try
        {
            callback();
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        if (!rejected)
        {
            throw std::runtime_error("Invalid resource operation succeeded at line " + std::to_string(location.line()));
        }
    }

    class TuningResource final : public Resource
    {
    public:
        explicit TuningResource(AssetId id) :
            Resource(id)
        {
        }

        std::string_view typeName() const override
        {
            return "TuningResource";
        }

        std::string title = "Untitled";
        AssetId     linked;
    };

    const ObjectTypeInfo& tuningType()
    {
        // Reuse the real generated scalar/reference contracts; only the test object's accessors differ.
        static const auto properties = []
        {
            auto title     = materialResourceType().property("name");
            title.name     = "title";
            title.jsonPath = "/title";
            title.get      = [](const void* object) -> PropertyValue
            {
                return static_cast<const TuningResource*>(object)->title;
            };
            title.set = [](void* object, const PropertyValue& value)
            {
                static_cast<TuningResource*>(object)->title = std::get<std::string>(value);
            };
            title.defaultValue = []() -> PropertyValue
            {
                return std::string("Untitled");
            };
            auto linked          = meshInstance3DType().property("model");
            linked.name          = "linked";
            linked.jsonPath      = "/linked";
            linked.referenceType = "TuningResource";
            linked.nullable      = true;
            linked.flags         = PropertyFlags::eSerialize | PropertyFlags::eInspect | PropertyFlags::eBind;
            linked.get           = [](const void* object) -> PropertyValue
            {
                return PropertyReference {static_cast<const TuningResource*>(object)->linked.value};
            };
            linked.set = [](void* object, const PropertyValue& value)
            {
                static_cast<TuningResource*>(object)->linked = {std::get<PropertyReference>(value).id};
            };
            linked.defaultValue = []() -> PropertyValue
            {
                return PropertyReference {};
            };
            return std::array {title, linked};
        }();
        static const ObjectTypeInfo type {"TuningResource", properties, &resourceType(), ObjectCategory::eResource};
        return type;
    }

    std::unique_ptr<Resource> createTuning(AssetId id, std::string_view state)
    {
        auto resource = std::make_unique<TuningResource>(id);
        deserializeProperties(tuningType(), state, resource.get());
        return resource;
    }

    std::string serializeTuning(const Resource& resource)
    {
        return serializeProperties(tuningType(), dynamic_cast<const void*>(&resource));
    }

    const SceneResourceType kTuning {tuningType(), &createTuning, &serializeTuning};

    void resourceWorld()
    {
        SceneTypeCatalog types;
        types.add(kTuning);
        require(types.objects().type("TuningResource").isA("Resource") &&
                    types.objects().type("MaterialResource").category == ObjectCategory::eResource,
                "Resources do not share the generated type catalog");
        requireFailure(
            [&]
            {
                types.add(kTuning);
            });
        requireFailure(
            [&]
            {
                types.createResource("Node", {StableId::generate()}, "{}");
            });
        requireFailure(
            [&]
            {
                types.createResource("ModelResource", {StableId::generate()}, "{}");
            });
        requireFailure(
            [&]
            {
                types.createResource("TuningResource", {StableId::generate()}, "{\"unknown\":1}");
            });

        {
            const SceneResourceType wrongType {tuningType(),
                                               [](AssetId id, std::string_view) -> std::unique_ptr<Resource>
                                               {
                                                   return std::make_unique<Resource>(id);
                                               },
                                               &serializeTuning};
            SceneTypeCatalog        broken;
            broken.add(wrongType);
            requireFailure(
                [&]
                {
                    broken.createResource("TuningResource", {StableId::generate()}, "{}");
                });
        }
        {
            const SceneResourceType wrongIdentity {tuningType(),
                                                   [](AssetId, std::string_view) -> std::unique_ptr<Resource>
                                                   {
                                                       return std::make_unique<TuningResource>(
                                                           AssetId {StableId::generate()});
                                                   },
                                                   &serializeTuning};
            SceneTypeCatalog        broken;
            broken.add(wrongIdentity);
            requireFailure(
                [&]
                {
                    broken.createResource("TuningResource", {StableId::generate()}, "{}");
                });
        }

        SceneTree     world(std::make_unique<Node>("Resources"), types);
        const AssetId firstId {StableId::generate()};
        auto&      first = world.addResource(types.createResource("TuningResource", firstId, "{\"title\":\"First\"}"));
        const auto secondId = sceneCreateResource(world, "TuningResource", "{\"title\":\"Second\"}");
        auto&      second   = *world.findResource(secondId);
        world.writeProperty(first.id(), "linked", PropertyReference {second.assetId().value});
        auto&      material = world.addMaterial(std::make_unique<MaterialResource>("Paint"));
        const auto revision = world.changes().materials;
        sceneWriteProperty(world, nullptr, material.id(), "name", "\"Renamed\"");
        require(world.changes().materials == revision + 1 && material.name() == "Renamed" &&
                    world.materials().size() == 1 && world.resources().size() == 3,
                "Reflected resource setter did not share material ownership/revisions");
        require(sceneObjectType(world, first.id()) == "TuningResource" &&
                    sceneFindResource(world, firstId.value.toString()) == first.id(),
                "Common resource identity lookup failed");
        requireFailure(
            [&]
            {
                world.addResource(types.createResource("Resource", firstId, "{}"));
            });
        requireFailure(
            [&]
            {
                world.writeProperty(first.id(), "linked", PropertyReference {material.assetId().value});
            });
        requireFailure(
            [&]
            {
                world.writeProperty(first.id(), "linked", PropertyReference {StableId::generate()});
            });
        requireFailure(
            [&]
            {
                world.writeProperty(first.id(), "title", true);
            });
        requireFailure(
            [&]
            {
                world.writeProperty(material.id(), "name", std::string {});
            });
        require(material.name() == "Renamed", "A rejected semantic write changed the resource");
        const auto resourceCount = world.resources().size();
        requireFailure(
            [&]
            {
                sceneCreateResource(world,
                                    "TuningResource",
                                    "{\"linked\":\"" + StableId::generate().toString() + "\"}");
            });
        require(world.resources().size() == resourceCount, "Failed resource construction published a partial object");

        SceneTree foreign(std::make_unique<Node>("Foreign"), types);
        auto&     foreignResource = foreign.addResource(types.createResource("Resource", {StableId::generate()}, "{}"));
        requireFailure(
            [&]
            {
                world.readProperty(foreignResource.id(), "name");
            });
        requireFailure(
            [&]
            {
                world.removeResource(foreignResource);
            });

        SceneAccess            access {.scene = &world, .serial = 1, .active = true};
        const VultraSceneFrame frame {&access, 1};
        uint64_t               size = 0;
        for (const std::string_view state : {"[", "[]", "{\"title\":4}"})
        {
            uint64_t created = 0;
            require(sceneApi().create_resource(frame, "TuningResource", 14, state.data(), state.size(), &created) ==
                            VULTRA_STATUS_INVALID_ARGUMENT &&
                        created == 0 && world.resources().size() == resourceCount,
                    "Malformed resource input has inconsistent ABI errors or leaked a partial object");
        }
        require(sceneApi().read_property(frame, first.id().value, "title", 5, nullptr, 0, &size) == VULTRA_STATUS_OK,
                "Generated ABI rejected a resource property");
        std::string value(size, '\0');
        require(sceneApi().read_property(frame, first.id().value, "title", 5, value.data(), value.size(), &size) ==
                        VULTRA_STATUS_OK &&
                    value == "\"First\"",
                "Generated resource read differs from C++");
        require(sceneApi().write_property(frame, first.id().value, "title", 5, "\"Edited\"", 8) == VULTRA_STATUS_OK,
                "Generated resource write failed");
        require(sceneApi().read_property(frame, foreignResource.id().value, "title", 5, nullptr, 0, &size) ==
                    VULTRA_STATUS_INVALID_ARGUMENT,
                "Generated resource API accepted an identity from another world");
        access.active = false;
        require(sceneApi().read_property(frame, first.id().value, "title", 5, nullptr, 0, &size) ==
                    VULTRA_STATUS_INVALID_FRAME,
                "Resource API accepted an expired callback borrow");
        access.active = true;
        ++access.serial;
        require(sceneApi().read_property(frame, first.id().value, "title", 5, nullptr, 0, &size) ==
                    VULTRA_STATUS_INVALID_FRAME,
                "An old resource frame became valid in a later callback");

        const auto  document = world.serialize();
        auto        copy     = SceneTree::parse(document, types);
        const auto* loaded   = copy.findResource(firstId);
        require(copy.serialize() == document && loaded && loaded->id() != first.id() &&
                    copy.readProperty(loaded->id(), "title") == PropertyValue(std::string("Edited")) &&
                    std::get<PropertyReference>(copy.readProperty(loaded->id(), "linked")).id == second.assetId().value,
                "Resource type/properties/references did not round trip with fresh runtime identities");
        const auto directory = std::filesystem::path("build/.tmp") / ("resources-" + StableId::generate().toString());
        std::filesystem::create_directories(directory);
        const auto file = directory / "scene.vscene";
        world.save(file);
        static_cast<TuningResource&>(first).linked = material.assetId();
        requireFailure(
            [&]
            {
                world.save(file);
            });
        std::ifstream input(file);
        require(std::string(std::istreambuf_iterator<char>(input), {}) == document,
                "A failed resource save replaced the previous scene document");
        input.close();
        static_cast<TuningResource&>(first).linked = second.assetId();
        world.save(file);
        require(SceneTree::load(file, types).serialize() == document,
                "A corrected resource document did not recover after a rejected save");
        auto invalid                              = nlohmann::json::parse(document);
        invalid["resources"][0]["data"]["linked"] = material.assetId().value.toString();
        requireFailure(
            [&]
            {
                SceneTree::parse(invalid.dump(), types);
            });
        invalid                         = nlohmann::json::parse(document);
        invalid["resources"][0]["type"] = "Node";
        requireFailure(
            [&]
            {
                SceneTree::parse(invalid.dump(), types);
            });
        invalid = nlohmann::json::parse(document);
        invalid["resources"].push_back(invalid["resources"][0]);
        requireFailure(
            [&]
            {
                SceneTree::parse(invalid.dump(), types);
            });

        world.writeProperty(first.id(), "linked", PropertyReference {});
        const auto retired = second.id();
        sceneRemoveResource(world, retired);
        require(!world.findObject(retired), "Removed resource retained its runtime identity");
        const VultraSceneFrame currentFrame {&access, access.serial};
        require(sceneApi().read_property(currentFrame, retired.value, "title", 5, nullptr, 0, &size) ==
                    VULTRA_STATUS_INVALID_ARGUMENT,
                "Generated resource API accepted a retired identity");
        requireFailure(
            [&]
            {
                sceneRemoveResource(world, retired);
            });
        requireFailure(
            [&]
            {
                world.readProperty(retired, "title");
            });
        const auto materialId = material.id();
        world.removeMaterial(material);
        require(!world.findResource(materialId) && world.materials().empty(), "Material index outlived its owner");
        types.remove(kTuning);
        require(world.objectType(first.id()).name == std::string_view("TuningResource"),
                "A world lost its copied project registrations");
    }
} // namespace

int main()
{
    resourceWorld();
}
