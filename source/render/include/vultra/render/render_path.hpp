#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/core/math/render_camera.hpp>
#include <vultra/graph/render_graph.hpp>
#include <vultra/render/render_light.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    class AssetSource;
    class Environment;
    class GpuScene;

    // Standard scene resources. Every render path provides the first two; passes after the path depend on names.
    inline constexpr std::string_view kSceneHdr       = "scene.hdr";       // RGBA16_SFLOAT, scene-linear, unexposed.
    inline constexpr std::string_view kSceneDepth     = "scene.depth";     // D32_SFLOAT, 0 near to 1 far.
    inline constexpr std::string_view kSceneVertices  = "scene.vertices";  // Buffer of SceneVertex.
    inline constexpr std::string_view kSceneIndices   = "scene.indices";   // Buffer of uint32 indices.
    inline constexpr std::string_view kSceneInstances = "scene.instances"; // Buffer of PrimitiveTransform.
    // A constant buffer of CameraData for one view.
    inline constexpr std::string_view kCameraData = "camera.data";

    // The constants of one view as passes read them. Keep in sync with VultraCameraData in the host's vultra.slang.
    struct CameraData
    {
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 viewProjection;
        glm::mat4 inverseView;
        glm::mat4 inverseProjection;
        glm::mat4 inverseViewProjection;
        glm::mat4 previousViewProjection; // Equal to viewProjection in the first frame of a graph.
        glm::vec4 position;               // xyz world position of the camera, w 1.
        glm::vec2 imageSize;              // In pixels.
        glm::vec2 jitter;                 // Subpixel offset in pixels; zero until a stage asks for jitter.
        float     nearPlane;
        float     farPlane;
        glm::vec2 padding;
    };

    static_assert(sizeof(CameraData) == 496);

    struct SceneResourceDesc
    {
        std::string name;
        VriFormat   format = VriFormat_Unknown; // Unknown for a buffer.
        std::string semantic;                   // Meaning beyond the format, e.g. "color.linear.hdr" or "depth".
        bool        buffer      = false;
        uint32_t    elementSize = 0; // A buffer's bytes per element.
    };

    struct SceneResource
    {
        std::string           name;
        RenderGraph::Resource resource;
    };

    using SceneResources = std::vector<SceneResource>;

    // Returns an error when the name is absent.
    [[nodiscard]] std::expected<RenderGraph::Resource, Error>
    findSceneResource(std::span<const SceneResource> resources, std::string_view name);

    // A frame stage runs once per graph before every view; a view stage runs once for each view.
    enum class StageScope
    {
        eFrame,
        eView
    };

    struct StageNeed
    {
        std::string name;
        // Without a provider the stage works as if the resource did not exist, e.g. lighting without shadows.
        bool optional = false;
    };

    // What a stage reads and writes, known without a device so that paths can be checked offline.
    struct StageDesc
    {
        std::string name; // With its namespace, e.g. "vultra.forward_shading".
        // The slots of a path it can fill. A stage with several fills all of them, next to each other in this order.
        std::vector<std::string>       slots;
        StageScope                     scope = StageScope::eView;
        std::vector<StageNeed>         needs;
        std::vector<SceneResourceDesc> provides;
    };

    // One image a path renders. With several views, view resources are named "<view>:<resource>" in the graph.
    struct RenderView
    {
        std::string name;
        Extent      size;
    };

    struct StageBuild
    {
        RenderGraph&                graph;
        GpuScene&                   scene;
        Environment&                environment;
        std::span<const RenderView> views;
        std::optional<uint32_t>     view; // The view a view stage builds; empty for a frame stage.
        // What earlier stages provided that this stage may read: frame resources and those of its own view.
        std::span<const SceneResource> resources;

        // The graph name of a resource or pass this stage adds: prefixed by the view's name when there are several.
        std::string graphName(std::string_view name) const;
    };

    struct StageFrame
    {
        std::span<const RenderCamera> cameras; // One per view.
        std::optional<uint32_t>       view;    // The view being prepared; empty for a frame stage.
        std::span<const RenderLight>  lights;
        const LightBuffer&            lightBuffer; // The path's scene.lights, written for this frame.
    };

    // A part of a render technique that fills one or more slots of a path. One instance serves every view of
    // a graph, so it keeps what differs per view apart by view index.
    class RenderStage
    {
    public:
        virtual ~RenderStage() = default;

        // Appends the stage's passes and returns exactly the resources its description provides. A frame stage
        // is called once per graph, a view stage once per view in view order. The stage borrows the scene and
        // environment until the last GPU use of the graph completes; a call for view 0 starts a new graph.
        virtual std::expected<SceneResources, Error> addPasses(StageBuild& build) = 0;

        // Once per frame after graph.compile() and before recording, after the previous frame completed:
        // once for a frame stage, once per view for a view stage.
        virtual std::expected<void, Error> prepare(RenderGraph& graph, const StageFrame& frame) = 0;

        // Between completed frames. Reloads changed shader sources; a failed reload keeps the last pipeline.
        virtual void pollShaders()
        {
        }
    };

    // `shaders` is the cooked builtin shader source, or null to compile builtin shaders from source.
    using CreateStage =
        std::function<std::expected<std::unique_ptr<RenderStage>, Error>(Device& device, const AssetSource* shaders)>;

    struct StageType
    {
        StageDesc   desc;
        CreateStage create;
    };

    struct PathSlot
    {
        std::string slot;
        std::string stage;
    };

    // A named, ordered list of slots and the stages that fill them. A stage that fills several slots appears
    // once per slot, in consecutive entries.
    struct RenderPathDesc
    {
        std::string           name;
        std::vector<PathSlot> slots;
    };

    enum class PathIssueKind
    {
        eUnknownStage,     // No stage of that name.
        eWrongSlot,        // The stage cannot fill that slot.
        ePartialStage,     // A stage with several slots does not fill all of them together, in order.
        eDuplicateSlot,    // Two entries name the same slot.
        eMissingNeed,      // A required resource nothing before the stage provides.
        eMissingStandard,  // The stages together do not provide scene.hdr and scene.depth.
        eDuplicateProvide, // Two stages provide the same resource.
        eFrameNeedsView    // A frame stage needs a resource of a view stage.
    };

    struct PathIssue
    {
        PathIssueKind kind;
        size_t        slot; // Index into the path's slots; the slot count for the path as a whole.
        std::string   message;
    };

    // What every path provides before its first stage: scene.lights once per frame and camera.data for each view.
    struct PathInput
    {
        SceneResourceDesc desc;
        StageScope        scope;
    };

    std::span<const PathInput> pathInputs();

    // Checks a path against the stage descriptions alone. No GPU object is created; hosts call it offline.
    std::vector<PathIssue> validateRenderPath(const RenderPathDesc& path, std::span<const StageDesc> stages);

    // A render path turns one scene into named graph resources by running its stages in order. Forward,
    // deferred or clustered renderers are different lists of stages; later passes stay independent of them.
    class RenderPath
    {
    public:
        // Creates the stages the path names, picked from `stages` by name. An invalid path is an error with
        // the first problem validateRenderPath() finds. Graph passes keep the stages' addresses.
        [[nodiscard]] static std::expected<std::unique_ptr<RenderPath>, Error>
        create(Device&                    device,
               RenderPathDesc             desc,
               std::span<const StageType> stages,
               const AssetSource*         shaders = nullptr);
        RenderPath(const RenderPath&)            = delete;
        RenderPath& operator=(const RenderPath&) = delete;

        const RenderPathDesc& desc() const;
        // Everything the stages provide, frame resources first.
        std::span<const SceneResourceDesc> provides() const;
        // The stage in a slot, or null.
        RenderStage* stage(std::string_view slot) const;

        // Appends the passes of every stage for these views and returns, per view, the resources it sees under
        // their plain names. Checks each stage's resources against its description. Calling it again rebinds
        // the path to another scene or graph.
        [[nodiscard]] std::expected<std::vector<SceneResources>, Error>
        addPasses(RenderGraph& graph, GpuScene& scene, Environment& environment, std::span<const RenderView> views);

        // Once per frame after graph.compile() and before recording, with one camera per view.
        [[nodiscard]] std::expected<void, Error>
        prepare(RenderGraph& graph, std::span<const RenderCamera> cameras, std::span<const RenderLight> lights);

        void pollShaders();

    private:
        struct Instance
        {
            StageDesc                    desc;
            std::vector<size_t>          slots; // Indices into the path's slots.
            std::unique_ptr<RenderStage> stage;
        };

        // The constants of one view and the camera of its last frame.
        struct View
        {
            std::unique_ptr<Buffer>  camera;
            Extent                   size {};
            std::optional<glm::mat4> previousViewProjection;
        };

        RenderPath(Device& device, RenderPathDesc desc);

        Device&                        m_Device;
        RenderPathDesc                 m_Desc;
        std::unique_ptr<LightBuffer>   m_Lights;
        std::vector<View>              m_Views; // Kept across graphs; a graph uses the first m_ViewCount.
        std::vector<Instance>          m_Stages;
        std::vector<SceneResourceDesc> m_Provides;
        size_t                         m_ViewCount = 0;
    };
} // namespace vultra
