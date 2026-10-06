#pragma once

#include <vultra/core/base/api_annotations.hpp>
#include <vultra/main/experiment_session.hpp>
#include <vultra/scene/scene_api.hpp>

#include <memory>

namespace vultra
{
    struct VULTRA_BIND_POD ExperimentImageInfo
    {
        uint32_t width;
        uint32_t height;
        uint64_t floatCount;
    };

    struct VULTRA_BIND_POD ExperimentPixel
    {
        float red;
        float green;
        float blue;
        float alpha;
    };

    struct VULTRA_BIND_POD ExperimentProgress
    {
        uint64_t frames;
        double   seconds;
        float    timeStep;
    };

    // Optional scripted sessions. Calls are synchronous and must be serialized on one thread.
    // Device, catalog and shader files are borrowed for the host lifetime. IDs are context-checked and never reused.
    class ExperimentHost
    {
    public:
        ExperimentHost(Device&               device,
                       const PassCatalog&    catalog,
                       std::filesystem::path shaderRoot,
                       AssetImportOptions    importOptions = {});
        ~ExperimentHost();
        ExperimentHost(const ExperimentHost&)            = delete;
        ExperimentHost& operator=(const ExperimentHost&) = delete;

        VULTRA_BIND_EXPERIMENT
        uint64_t open(std::string_view input,
                      uint32_t         width,
                      uint32_t         height,
                      uint32_t         path,
                      float            timeStep,
                      std::string_view environment,
                      uint32_t         seed = 0);
        VULTRA_BIND_EXPERIMENT
        uint64_t runDescription(std::string_view file);
        VULTRA_BIND_EXPERIMENT
        void close(uint64_t session);
        VULTRA_BIND_EXPERIMENT
        void step(uint64_t session, uint64_t frames);
        VULTRA_BIND_EXPERIMENT
        void setGraph(uint64_t session, std::string_view definition);
        VULTRA_BIND_EXPERIMENT
        void setParameter(uint64_t session, std::string_view pass, std::string_view parameter, double value);
        VULTRA_BIND_EXPERIMENT
        ExperimentImageInfo imageInfo(uint64_t session, std::string_view output);
        VULTRA_BIND_EXPERIMENT
        void readImage(uint64_t session, std::string_view output, std::span<float> pixels);
        VULTRA_BIND_EXPERIMENT
        void readPreview(uint64_t         session,
                         std::string_view output,
                         uint32_t         channel,
                         float            minimum,
                         float            maximum,
                         std::span<float> pixels);
        VULTRA_BIND_EXPERIMENT
        ExperimentPixel probePixel(uint64_t session, std::string_view output, uint32_t x, uint32_t y);
        VULTRA_BIND_EXPERIMENT
        ExperimentProgress progress(uint64_t session) const;
        VULTRA_BIND_EXPERIMENT
        std::string_view sceneSnapshot(uint64_t session);
        VULTRA_BIND_EXPERIMENT
        std::string_view reportSnapshot(uint64_t session);
        VULTRA_BIND_EXPERIMENT
        CameraSettings cameraSettings(uint64_t session, std::string_view node);
        VULTRA_BIND_EXPERIMENT
        void setCameraSettings(uint64_t session, std::string_view node, CameraSettings settings);
        VULTRA_BIND_EXPERIMENT
        LightSettings lightSettings(uint64_t session, std::string_view node);
        VULTRA_BIND_EXPERIMENT
        void setLightSettings(uint64_t session, std::string_view node, LightSettings settings);
        VULTRA_BIND_EXPERIMENT
        EnvironmentSettings environmentSettings(uint64_t session, std::string_view node);
        VULTRA_BIND_EXPERIMENT
        void setEnvironmentSettings(uint64_t session, std::string_view node, EnvironmentSettings settings);
        VULTRA_BIND_EXPERIMENT
        MaterialParameters materialParameters(uint64_t session, std::string_view material);
        VULTRA_BIND_EXPERIMENT
        void setMaterialParameters(uint64_t session, std::string_view material, MaterialParameters parameters);
        // Column-major affine matrix, exactly 16 finite floats. Edits affect the next completed step.
        VULTRA_BIND_EXPERIMENT
        void readTransform(uint64_t session, std::string_view node, std::span<float> values);
        VULTRA_BIND_EXPERIMENT
        void setTransform(uint64_t session, std::string_view node, std::span<float> values);
        void recordError(std::string_view operation, std::string_view message) noexcept;
        VULTRA_BIND_EXPERIMENT
        std::string_view lastError() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

} // namespace vultra
