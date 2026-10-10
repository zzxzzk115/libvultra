#include <vultra/core/math/rigid_pose.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/builtin/render_properties.generated.hpp>
#include <vultra/servers/rendering/research/research_configuration.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace vultra
{
    void ResearchConfiguration::validate() const
    {
        camera.validate();
        validateRigidPose(glm::inverse(rigView));
        const auto readable = glm::inverse(camera.camera(sizes[0]).view);
        const auto exact    = glm::inverse(rigView);
        for (size_t column = 0; column < 4; ++column)
        {
            for (size_t row = 0; row < 4; ++row)
            {
                if (std::abs(readable[column][row] - exact[column][row]) > 1e-4f)
                {
                    throw std::invalid_argument("Configuration camera pose disagrees with its exact rigView");
                }
            }
        }
        validateRigidPose(trackingPose);
        if (headset)
        {
            headset->validate();
        }
        if (track)
        {
            track->validate();
        }
        if (project.empty() || methods[0].name.empty() || methods[1].name.empty() || !std::isfinite(ipd) || ipd <= 0 ||
            ipd > 0.2f || !std::isfinite(xrScale) || xrScale < 0.25f || xrScale > 1 || !std::isfinite(differenceGain) ||
            differenceGain <= 0 || view < 0 || view > 3 || !std::isfinite(pixelsPerDegree) || pixelsPerDegree < 1 ||
            pixelsPerDegree > 1000 || renderer.path == RenderPath::eReferencePathTracing ||
            renderer.toneOperator > ToneOperator::eReinhard ||
            glm::dot(renderer.directionToLight, renderer.directionToLight) <= 0)
        {
            throw std::invalid_argument("Invalid research configuration identity, camera, display or render settings");
        }
        for (const auto size : sizes)
        {
            if (size.width < 11 || size.height < 11 || size.width > 8192 || size.height > 8192)
            {
                throw std::invalid_argument("Research eye extent must be between 11 and 8192");
            }
            validateMetricRegion(size, roi);
        }
        for (const auto& method : methods)
        {
            for (const auto& [id, parameters] : method.parameters)
            {
                if (id.empty())
                {
                    throw std::invalid_argument("Configuration Pass ID is empty");
                }
                for (const auto& [name, value] : parameters)
                {
                    if (name.empty() || !std::isfinite(value))
                    {
                        throw std::invalid_argument("Configuration parameter is unnamed or non-finite");
                    }
                }
            }
        }
    }

    std::string ResearchConfiguration::serialize() const
    {
        validate();
        const CameraTrack     pose {{{0, camera}}};
        std::array<float, 16> tracking;
        std::array<float, 16> viewMatrix;
        for (size_t i = 0; i < tracking.size(); ++i)
        {
            tracking[i]   = trackingPose[i / 4][i % 4];
            viewMatrix[i] = rigView[i / 4][i % 4];
        }
        nlohmann::json doc {
            {"format", "vultra.research_configuration"},
            {"version", 1},
            {"project", project},
            {"engineHash", engineHash},
            {"modelOverride", modelOverride.generic_string()},
            {"environmentOverride", environmentOverride.generic_string()},
            {"renderer", nlohmann::json::parse(serializeProperties(renderSettingsType(), &renderer))},
            {"camera", nlohmann::json::parse(pose.serialize())["keys"][0]},
            {"headset", headset ? nlohmann::json::parse(headset->serialize()) : nlohmann::json(nullptr)},
            {"track", track ? nlohmann::json::parse(track->serialize()) : nlohmann::json(nullptr)},
            {"trackFrame", trackFrame},
            {"trackingPose", tracking},
            {"rigView", viewMatrix},
            {"sizes", {{sizes[0].width, sizes[0].height}, {sizes[1].width, sizes[1].height}}},
            {"ipd", ipd},
            {"xrScale", xrScale},
            {"differenceGain", differenceGain},
            {"view", view},
            {"referenceSnapshot", referenceSnapshot},
            {"roi", {roi.x, roi.y, roi.width, roi.height}},
            {"pixelsPerDegree", pixelsPerDegree},
            {"masks", masks}};
        for (const auto& method : methods)
        {
            doc["methods"].push_back({{"name", method.name}, {"parameters", method.parameters}});
        }
        return doc.dump(2);
    }

    ResearchConfiguration ResearchConfiguration::parse(std::string_view text)
    {
        const auto doc = nlohmann::json::parse(text);
        if (doc.at("format") != "vultra.research_configuration" || doc.at("version") != 1)
        {
            throw std::invalid_argument("Unsupported research configuration format");
        }
        ResearchConfiguration result;
        result.project             = doc.at("project").get<std::string>();
        result.engineHash          = doc.at("engineHash").get<std::string>();
        result.modelOverride       = doc.at("modelOverride").get<std::string>();
        result.environmentOverride = doc.at("environmentOverride").get<std::string>();
        deserializeProperties(renderSettingsType(), doc.at("renderer").dump(), &result.renderer);
        result.camera = CameraTrack::parse(nlohmann::json {{"format", "vultra.camera_track"},
                                                           {"version", 1},
                                                           {"keys", nlohmann::json::array({doc.at("camera")})}}
                                               .dump())
                            .keys.front()
                            .pose;
        if (!doc.at("headset").is_null())
        {
            result.headset = HeadsetProfile::parse(doc.at("headset").dump());
        }
        if (!doc.at("track").is_null())
        {
            result.track = CameraTrack::parse(doc.at("track").dump());
        }
        if (!doc.at("trackFrame").is_number_unsigned() || doc.at("methods").size() != 2)
        {
            throw std::invalid_argument("Configuration needs an unsigned track frame and exactly two methods");
        }
        result.trackFrame     = doc.at("trackFrame").get<uint64_t>();
        const auto tracking   = doc.at("trackingPose").get<std::array<float, 16>>();
        const auto viewMatrix = doc.at("rigView").get<std::array<float, 16>>();
        for (size_t i = 0; i < tracking.size(); ++i)
        {
            result.trackingPose[i / 4][i % 4] = tracking[i];
            result.rigView[i / 4][i % 4]      = viewMatrix[i];
        }
        const auto sizes = doc.at("sizes").get<std::array<std::array<int64_t, 2>, 2>>();
        for (size_t i = 0; i < 2; ++i)
        {
            if (sizes[i][0] < 11 || sizes[i][0] > 8192 || sizes[i][1] < 11 || sizes[i][1] > 8192)
            {
                throw std::invalid_argument("Invalid research configuration extent");
            }
            result.sizes[i]    = {uint32_t(sizes[i][0]), uint32_t(sizes[i][1])};
            const auto& method = doc.at("methods").at(i);
            result.methods[i] = {method.at("name").get<std::string>(), method.at("parameters").get<MethodParameters>()};
        }
        result.ipd               = doc.at("ipd").get<float>();
        result.xrScale           = doc.at("xrScale").get<float>();
        result.differenceGain    = doc.at("differenceGain").get<float>();
        result.view              = doc.at("view").get<int>();
        result.referenceSnapshot = doc.at("referenceSnapshot").get<bool>();
        const auto roi           = doc.at("roi").get<std::array<uint32_t, 4>>();
        result.roi               = {roi[0], roi[1], roi[2], roi[3]};
        result.pixelsPerDegree   = doc.at("pixelsPerDegree").get<float>();
        result.masks             = doc.at("masks").get<std::array<std::string, 2>>();
        result.validate();
        return result;
    }

    void ResearchConfiguration::save(const std::filesystem::path& path) const
    {
        const auto text = serialize() + '\n';
        writeFileAtomically(path, std::as_bytes(std::span(text)));
    }

    ResearchConfiguration ResearchConfiguration::load(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        return parse(std::string(std::istreambuf_iterator<char>(input), {}));
    }
} // namespace vultra
