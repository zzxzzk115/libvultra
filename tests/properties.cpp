#include <vultra/assets/material_properties.generated.hpp>
#include <vultra/scene/render_properties.generated.hpp>
#include <vultra/servers/rendering/builtin/render_properties.generated.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <limits>
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

    template<typename Callback>
    std::string requireFailure(Callback&& callback)
    {
        try
        {
            callback();
        }
        catch (const std::exception& error)
        {
            return error.what();
        }
        throw std::runtime_error("Invalid property operation was accepted");
    }
} // namespace

int main()
try
{
    using namespace vultra;
    ObjectTypeCatalog    first;
    ObjectTypeCatalog    second;
    const ObjectTypeInfo unnamed {nullptr, {}};
    requireFailure(
        [&]
        {
            first.add(unnamed);
        });
    first.add(cameraSettingsType());
    second.add(cameraSettingsType());
    requireFailure(
        [&]
        {
            first.add(cameraSettingsType());
        });
    first.remove(cameraSettingsType());
    requireFailure(
        [&]
        {
            first.type("CameraSettings");
        });
    require(&second.type("CameraSettings") == &cameraSettingsType(), "Type catalogs share registration state");
    requireFailure(
        [&]
        {
            first.remove(cameraSettingsType());
        });

    requireFailure(
        [&]
        {
            serializeProperties(cameraSettingsType(), nullptr);
        });
    CameraSettings camera;
    const auto&    near = cameraSettingsType().property("nearPlane");
    near.write(&camera, 0.1f);
    require(camera.nearPlane == 0.1f && std::get<float>(near.read(&camera)) == 0.1f,
            "Named property read/write did not reach the actual field");
    require(std::get<float>(near.defaultValue()) == CameraSettings {}.nearPlane,
            "Generated defaults differ from C++ defaults");
    requireFailure(
        [&]
        {
            near.write(&camera, 0.2);
        });
    requireFailure(
        [&]
        {
            near.write(&camera, std::numeric_limits<float>::infinity());
        });
    requireFailure(
        [&]
        {
            near.read(nullptr);
        });
    requireFailure(
        [&]
        {
            cameraSettingsType().property("missing");
        });
    const auto accepted = camera;
    requireFailure(
        [&]
        {
            deserializeProperties(cameraSettingsType(), R"({"near":0.3,"far":true})", &camera);
        });
    require(camera == accepted, "Failed JSON decoding partially mutated the settings");
    requireFailure(
        [&]
        {
            deserializeProperties(cameraSettingsType(), R"({"neer":0.3})", &camera);
        });
    deserializeProperties(cameraSettingsType(), R"({"near":0.25})", &camera);
    require(camera.nearPlane == 0.25f && camera.farPlane == CameraSettings {}.farPlane,
            "Missing serialized fields did not use C++ defaults");
    CameraNode node("Camera");
    auto       invalid = camera;
    invalid.nearPlane  = invalid.farPlane;
    requireFailure(
        [&]
        {
            node.setSettings(invalid);
        });
    require(node.settings() == CameraSettings {}, "Property descriptors bypassed the scene validation boundary");

    MaterialParameters material;
    material.baseRed        = 0.4f;
    material.coatWeight     = 0.5f;
    const auto materialJson = serializeProperties(materialParametersType(), &material);
    const auto data         = nlohmann::json::parse(materialJson);
    require(data.at("base_color").size() == 4 && data.at("coat_weight") == 0.5f,
            "Generated JSON paths lost the scene material layout");
    MaterialParameters copy;
    deserializeProperties(materialParametersType(), materialJson, &copy);
    require(serializeProperties(materialParametersType(), &copy) == materialJson, "Material round trip changed values");
    LightSettings light;
    requireFailure(
        [&]
        {
            deserializeProperties(lightSettingsType(), R"({"color":[1,1]})", &light);
        });

    RenderSettings renderer;
    renderer.path             = RenderPath::eReferencePathTracing;
    renderer.lightColor       = {0.2f, 0.4f, 0.8f};
    renderer.meshShading      = true;
    renderer.shadowFilter     = ShadowFilter::ePcss;
    renderer.shadowResolution = 2048;
    RenderSettings reopened;
    deserializeProperties(renderSettingsType(), serializeProperties(renderSettingsType(), &renderer), &reopened);
    require(reopened.path == renderer.path && reopened.lightColor == renderer.lightColor && reopened.meshShading &&
                reopened.shadowFilter == renderer.shadowFilter &&
                reopened.shadowResolution == renderer.shadowResolution,
            "Renderer round trip lost uninspected or non-scalar fields");
    requireFailure(
        [&]
        {
            renderSettingsType().property("path").write(&renderer, PropertyEnum {90});
        });
    requireFailure(
        [&]
        {
            renderSettingsType().property("shadowResolution").write(&renderer, uint64_t {1} << 32);
        });
    requireFailure(
        [&]
        {
            deserializeProperties(renderSettingsType(), R"({"shadowResolution":-1})", &renderer);
        });
    requireFailure(
        [&]
        {
            deserializeProperties(renderSettingsType(), R"({"shadowResolution":1.5})", &renderer);
        });
    requireFailure(
        [&]
        {
            deserializeProperties(renderSettingsType(), R"({"lightColor":[1,2,3,4]})", &renderer);
        });
    std::cout
        << "Property tests passed: named access, catalogs, defaults, strict/atomic decoding and settings parity\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
