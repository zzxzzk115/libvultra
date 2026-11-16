#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/core/math/render_camera.hpp>
#include <vultra/graph/pass_catalog.hpp>
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
    inline constexpr std::string_view kSceneHdr   = "scene.hdr";   // RGBA16_SFLOAT, scene-linear, unexposed.
    inline constexpr std::string_view kSceneDepth = "scene.depth"; // D32_SFLOAT, 0 near to 1 far.
    // The surface layers of the visible pixels. Paths provide them on request (StageBuild::requested), in the
    // formats of gbufferLayers(); shaders encode and decode them with `import vultra.gbuffer;`.
    inline constexpr std::string_view kSceneNormal   = "scene.normal";   // Octahedral unit world normal.
    inline constexpr std::string_view kSceneAlbedo   = "scene.albedo";   // Base color times base weight, occlusion.
    inline constexpr std::string_view kSceneMaterial = "scene.material"; // Roughness, metalness and more (full).
    inline constexpr std::string_view kSceneEmission = "scene.emission"; // Emitted radiance.
    inline constexpr std::string_view kSceneCoat     = "scene.coat";     // Full profile: coat weight, roughness, IOR.
    inline constexpr std::string_view kSceneSpecular = "scene.specular"; // Full profile: specular color, IOR.
    // RG16_SFLOAT: from a pixel to where its surface was in the previous frame, in UV units (x right, y down),
    // with camera and object motion and without jitter.
    inline constexpr std::string_view kSceneMotion = "scene.motion";
    // RGBA16_SFLOAT, color.linear.hdr: the image at output size that the project's graph follows and the display
    // stage reads; scene.hdr itself when no stage of the path provides it.
    inline constexpr std::string_view kSceneOutput = "scene.output";
    // The display stage's image, in the format the host presents.
    inline constexpr std::string_view kFinal = "final";
    // The last slot of a path: its stage turns scene.output into final after the project's graph has run.
    inline constexpr std::string_view kDisplaySlot    = "display";
    inline constexpr std::string_view kSceneVertices  = "scene.vertices";  // Buffer of SceneVertex.
    inline constexpr std::string_view kSceneIndices   = "scene.indices";   // Buffer of uint32 indices.
    inline constexpr std::string_view kSceneInstances = "scene.instances"; // Buffer of PrimitiveTransform.
    // The scene's materials: a MaterialHeader (type and parameter offset) per material number, and the parameter
    // blocks as 32-bit words. Lighting passes read them to call a material type's shade() (vultra.materials).
    inline constexpr std::string_view kSceneMaterials    = "scene.materials";
    inline constexpr std::string_view kSceneMaterialData = "scene.material_data";
    // RG32_UINT, surface.id: the material number and the primitive of the surface each pixel sees.
    inline constexpr std::string_view kSceneSurfaceId = "scene.surface_id";
    // A constant buffer of CameraData for one view.
    inline constexpr std::string_view kCameraData = "camera.data";
    // The image-based light of the scene's environment (Environment), equirectangular RGBA16_SFLOAT textures every
    // path imports once per frame: the radiance itself, its cosine-convolved irradiance, its specular prefilter
    // with one mip per roughness step, and the split-sum BRDF table (128 x 128, red scale, green bias).
    inline constexpr std::string_view kSceneEnvironment         = "scene.environment";
    inline constexpr std::string_view kSceneEnvironmentDiffuse  = "scene.environment_diffuse";
    inline constexpr std::string_view kSceneEnvironmentSpecular = "scene.environment_specular";
    inline constexpr std::string_view kSceneEnvironmentBrdf     = "scene.environment_brdf";

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

    // The G-buffer layers forward shading writes after scene.hdr, in this order, for a device of `api`. WebGPU gets
    // the compact profile, which fits its default 32 bytes of color attachments per sample with scene.hdr and has
    // no scene.coat or scene.specular; the other APIs get the full profile, which keeps every value at the
    // precision of its source. A stage that declares a layer with the format "standard" gets this format.
    [[nodiscard]] std::span<const SceneResourceDesc> gbufferLayers(VriGraphicsAPI api);

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
        // Values a user may change, shown and set as "<slot>.<name>"; RenderPath::setParameter changes them.
        std::vector<PassParameter> parameters;
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
        // What this stage must return of what it provides: scene.hdr, scene.depth, what later stages of the path
        // need (optional needs included) and what the caller of RenderPath::addPasses asked for. A stage may
        // leave out any other resource it provides, so that nothing renders what no one reads.
        std::span<const std::string> requested;
        // For the display slot: the format final must have. A display stage that cannot produce it fails.
        VriFormat displayFormat = VriFormat_Unknown;

        bool requests(std::string_view name) const;

        // The graph name of a resource or pass this stage adds: prefixed by the view's name when there are several.
        std::string graphName(std::string_view name) const;
    };

    struct StageFrame
    {
        std::span<const RenderCamera> cameras; // One per view.
        std::optional<uint32_t>       view;    // The view being prepared; empty for a frame stage.
        std::span<const RenderLight>  lights;
        const LightBuffer&            lightBuffer; // The path's scene.lights, written for this frame.
        std::span<const double>       parameters;  // This stage's values, in the order of its parameters.
    };

    // A part of a render technique that fills one or more slots of a path. One instance serves every view of
    // a graph, so it keeps what differs per view apart by view index.
    class RenderStage
    {
    public:
        virtual ~RenderStage() = default;

        // Appends the stage's passes and returns resources its description provides, at least those
        // build.requested names. A frame stage
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
        eFrameNeedsView,   // A frame stage needs a resource of a view stage.
        eDisplayNotLast    // The display slot is not the last one.
    };

    struct PathIssue
    {
        PathIssueKind kind;
        size_t        slot; // Index into the path's slots; the slot count for the path as a whole.
        std::string   message;
    };

    // What every path provides before its first stage: scene.lights and the environment textures once per frame,
    // camera.data for each view.
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
        // What the project's graph can read: the path's inputs and what the stages before the display slot
        // provide, frame resources first, with scene.output.
        std::span<const SceneResourceDesc> provides() const;
        // The stage in a slot, or null.
        RenderStage* stage(std::string_view slot) const;

        // Appends the passes of every stage before the display slot for these views and returns, per view, the
        // resources it sees under their plain names, scene.output among them. `requested` names resources the caller
        // reads beyond scene.hdr and scene.depth; a stage may leave out what neither the caller nor a later stage
        // reads. Checks each stage's resources against its description. Calling it again rebinds the path to another
        // scene or graph.
        [[nodiscard]] std::expected<std::vector<SceneResources>, Error>
        addPasses(RenderGraph&                 graph,
                  GpuScene&                    scene,
                  Environment&                 environment,
                  std::span<const RenderView>  views,
                  std::span<const std::string> requested = {});

        // Appends the display stage after the project's graph: `outputs` holds, per view, the image it shows
        // (scene.output or the graph's first output). Returns final per view, in `format`. An error when the path
        // has no display slot.
        [[nodiscard]] std::expected<std::vector<RenderGraph::Resource>, Error>
        addDisplay(RenderGraph& graph, std::span<const RenderGraph::Resource> outputs, VriFormat format);

        // The current value of a stage parameter, and its change. An unknown slot or name, or a value outside
        // the parameter's range, is an error and changes nothing.
        [[nodiscard]] std::expected<double, Error> parameter(std::string_view slot, std::string_view name) const;
        [[nodiscard]] std::expected<void, Error>
        setParameter(std::string_view slot, std::string_view name, double value);

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
            std::vector<double>          parameters;    // Current values, in the order of desc.parameters.
            bool                         built = false; // Whether this graph has the stage's passes.
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
        std::vector<RenderView>        m_ViewDescs; // The views of the current graph, for the display stage.
        GpuScene*                      m_Scene       = nullptr; // Of the current graph.
        Environment*                   m_Environment = nullptr;
    };
} // namespace vultra
