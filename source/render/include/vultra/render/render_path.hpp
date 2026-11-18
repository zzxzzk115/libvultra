#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/core/math/render_camera.hpp>
#include <vultra/graph/pass_catalog.hpp>
#include <vultra/graph/render_graph.hpp>
#include <vultra/render/render_light.hpp>

#include <expected>
#include <filesystem>
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
    inline constexpr std::string_view kDisplaySlot = "display";
    // R32_SFLOAT, shadow.mask: how much of the main directional light each pixel of a view does not receive, 1 in
    // full shadow; zero where no stage provides it.
    inline constexpr std::string_view kSceneShadowMask = "scene.shadow_mask";
    // The slot that turns the render size into the output size: the stages up to it render at a view's render size,
    // the stages after it at its output size.
    inline constexpr std::string_view kUpscaleSlot    = "upscale";
    inline constexpr std::string_view kSceneVertices  = "scene.vertices";  // Buffer of SceneVertex.
    inline constexpr std::string_view kSceneIndices   = "scene.indices";   // Buffer of uint32 indices.
    inline constexpr std::string_view kSceneInstances = "scene.instances"; // Buffer of InstanceData.
    inline constexpr std::string_view kSceneModels    = "scene.models";    // Buffer of ModelData.
    // The scene's materials: a MaterialHeader (type and parameter offset) per material number, and the parameter
    // blocks as 32-bit words. Lighting passes read them to call a material type's shade() (vultra.materials).
    inline constexpr std::string_view kSceneMaterials    = "scene.materials";
    inline constexpr std::string_view kSceneMaterialData = "scene.material_data";
    // R32_UINT, surface.id: the material number of the surface each pixel sees.
    inline constexpr std::string_view kSceneSurfaceId = "scene.surface_id";
    // A constant buffer of CameraData for one view.
    inline constexpr std::string_view kCameraData = "camera.data";
    // A constant buffer of ViewsData: every view of the path, for passes that move pixels from one view to another.
    // A frame resource of paths with at most kViewsDataViews views.
    inline constexpr std::string_view kViewsData = "views.data";
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
        glm::vec2 imageSize;              // The render size in pixels.
        // The subpixel offset of this frame's projection, in pixels of the render size, x right and y down; zero
        // unless a stage of the path asks for jitter. previousViewProjection has none, so motion leaves it out.
        glm::vec2 jitter;
        float     nearPlane;
        float     farPlane;
        glm::vec2 outputSize; // In pixels; the render size unless the path has an upscale slot.
        // The tangents of the left, right, up and down half angles of the projection without jitter, each positive
        // when that edge lies on its own side of the view axis; zero for an orthographic projection. The frustum may
        // be asymmetric: derive nothing from a vertical field of view.
        glm::vec4 tanHalfAngles;
        uint32_t  viewIndex; // Of this view among the graph's views: the path's own first, then auxiliary ones.
        uint32_t  viewCount; // The path's own views.
        glm::vec2 padding;   // Keeps the size a multiple of 16 bytes, as constant buffers lay it out.
    };

    static_assert(sizeof(CameraData) == 528);

    // One view in ViewsData. The current matrices are those of camera.data, jitter included; the previous ones are
    // last frame's without jitter, equal to the current ones without jitter in the first frame of a graph.
    struct ViewData
    {
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 viewProjection;
        glm::mat4 inverseView;
        glm::mat4 inverseProjection;
        glm::mat4 inverseViewProjection;
        glm::mat4 previousView;
        glm::mat4 previousProjection;
        glm::mat4 previousViewProjection;
        glm::vec4 position;      // xyz world position of the camera, w 1.
        glm::vec4 tanHalfAngles; // As in CameraData.
    };

    inline constexpr uint32_t kViewsDataViews = 2;

    // The path's own views as passes read them; keep in sync with VultraViewsData in the host's vultra.slang.
    struct ViewsData
    {
        std::array<ViewData, kViewsDataViews> views; // views[viewCount] onwards are zero.
        uint32_t                              viewCount;
        std::array<uint32_t, 3>               padding; // Keeps the size a multiple of 16 bytes.
    };

    static_assert(sizeof(ViewsData) == 1232);

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

    // A view a stage renders besides the path's own, such as a shadow map's light view: the path runs the stages of
    // its slots for it. Its size and camera come from the stage when it builds.
    struct AuxiliaryViewDesc
    {
        std::string              name;
        std::vector<std::string> slots; // Slots of the path, usually only "visibility".
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
        // The parameters addPasses() reads, such as the size of an image: a change takes effect when the graph is
        // built again, and RenderPath::needsBuild() tells the caller so.
        std::vector<std::string> buildParameters;
        // The subpixel jitter the stage asks for on every view's projection: the length of the Halton (2, 3)
        // sequence it repeats; 0 for none. At most one stage of a path asks.
        uint32_t jitter = 0;
        // A stage in the upscale slot: the render scales (render size over output size) it accepts.
        float minimumScale = 1;
        float maximumScale = 1;
        // A view stage: the auxiliary views it renders for each of the path's views. The stages in their slots may
        // not ask for views of their own.
        std::vector<AuxiliaryViewDesc> auxiliaryViews;
        // VRI feature bits (VriFeature_*) the stage uses when the device has them and does without otherwise, such as
        // mesh shading; a host requests them, best effort, when it creates the device for a path with the stage. The
        // stage tells which way it took.
        uint64_t optionalFeatures = 0;
        // A geometry stage: the geometry processor whose buffers it reads, run on every model when it is imported.
        // The caller resolves the name; the kernel keeps no registry of processors.
        std::string geometryProcessor;
    };

    struct StageBuild;

    // This frame's camera of an auxiliary view, from the camera of the view it was asked for and the scene's lights.
    using AuxiliaryCamera = std::function<RenderCamera(const RenderCamera& view, std::span<const RenderLight> lights)>;

    // One image a path renders. With several views, view resources are named "<view>:<resource>" in the graph.
    struct RenderView
    {
        std::string name;
        Extent      size; // The output size: scene.output and what follows it.
        // The size the stages up to the upscale slot render at; empty for the output size. It differs from it only
        // in a path with an upscale slot whose stage accepts the scale.
        Extent renderSize {};
        // A derived view runs none of the path's view stages: addPasses() gives it the frame's resources and its
        // camera.data, and the caller makes its scene.hdr, for example from other views' resources, before
        // addDisplay(). The first view is never derived.
        bool derived = false;
        // A derived view: the slots whose view stages run for it all the same, before its scene.hdr is made, so that
        // it has its own depth or surface ids, say. The caller checks which slots may be listed.
        std::vector<std::string> slots;
    };

    struct StageBuild
    {
        RenderGraph& graph;
        GpuScene&    scene;
        Environment& environment;
        // Every view of the graph: the path's own, then the auxiliary views its stages render.
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
        // A view stage: the size it renders its view at, the render size up to the upscale slot and the output size
        // after it, and the view's output size, which the upscale stage writes. Empty for a frame stage.
        Extent size {};
        Extent outputSize {};
        // A view stage of one of the path's views: renders the auxiliary view `name` its description declares, at
        // `size` with `camera`, by running the stages of its slots, and returns what they provide, camera.data among
        // it, as "<name>:<resource>". Empty for a stage that renders an auxiliary view itself.
        std::function<std::expected<SceneResources, Error>(std::string_view name, Extent size, AuxiliaryCamera camera)>
            addView;
        // How many of `views` are the path's own; zero when all are.
        size_t pathViews = 0;
        // This stage's values, in the order of its parameters.
        std::span<const double> parameters;

        bool requests(std::string_view name) const;

        // The graph name of a resource or pass this stage adds: prefixed by the view's name when the path has several
        // views or the view is auxiliary.
        std::string graphName(std::string_view name) const;
    };

    struct StageFrame
    {
        std::span<const RenderCamera> cameras; // One per view.
        std::optional<uint32_t>       view;    // The view being prepared; empty for a frame stage.
        std::span<const RenderLight>  lights;
        const LightBuffer&            lightBuffer; // The path's scene.lights, written for this frame.
        std::span<const double>       parameters;  // This stage's values, in the order of its parameters.
        // `view` is an auxiliary view: no motion feedback, no jitter, no temporal accumulation.
        bool auxiliary = false;
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
    // What a stage factory compiles its shaders with. A view, valid only during the factory call: a stage copies the
    // paths it keeps, for a reload say.
    struct StageSources
    {
        const AssetSource* shaders = nullptr; // Cooked builtin shaders; null compiles them from the working directory.
        // Where its shaders' imports are found: a host's project shaders first (its generated vultra.materials),
        // then the builtin shaders' roots. Empty for the builtin roots alone.
        std::span<const std::filesystem::path> includeDirectories;
    };

    using CreateStage =
        std::function<std::expected<std::unique_ptr<RenderStage>, Error>(Device& device, const StageSources& sources)>;

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
        eDisplayNotLast,   // The display slot is not the last one.
        eDuplicateJitter,  // More than one stage asks for jitter.
        eAuxiliaryView     // An auxiliary view names a slot the path does not have, one after its stage, a frame
                           // stage's, or one whose stage asks for auxiliary views itself.
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
        [[nodiscard]] static std::expected<std::unique_ptr<RenderPath>, Error> create(Device&                    device,
                                                                                      RenderPathDesc             desc,
                                                                                      std::span<const StageType> stages,
                                                                                      const StageSources& sources = {});
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
        // Whether a parameter that a stage reads in addPasses() changed since the graph was built: the caller builds
        // the graph again for the change to take effect.
        bool needsBuild() const;

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
            std::vector<uint32_t>        auxiliary;     // The auxiliary views this graph runs the stage for.
        };

        // An auxiliary view of this graph: the view it is rendered for and its camera, once its requester built it.
        struct Auxiliary
        {
            uint32_t        view;
            size_t          requester; // Index into m_Stages.
            std::string     name;
            AuxiliaryCamera camera;
        };

        // The constants of one view and the camera of its last frame, without jitter.
        struct View
        {
            std::unique_ptr<Buffer>  camera;
            Extent                   size {};
            Extent                   renderSize {};
            std::optional<glm::mat4> previousView; // Last frame's, without jitter.
            std::optional<glm::mat4> previousProjection;
            uint32_t                 frame = 0; // Frames prepared since the graph was built, for the jitter.
        };

        RenderPath(Device& device, RenderPathDesc desc);

        Device&                        m_Device;
        RenderPathDesc                 m_Desc;
        std::unique_ptr<LightBuffer>   m_Lights;
        std::unique_ptr<Buffer>        m_ViewsData; // views.data.
        std::vector<View>              m_Views;     // Kept across graphs; a graph uses the first m_ViewCount.
        std::vector<Instance>          m_Stages;
        std::vector<SceneResourceDesc> m_Provides;
        size_t                         m_ViewCount = 0;
        std::vector<RenderView>        m_ViewDescs; // The views of the current graph, auxiliary ones after the path's.
        std::vector<Auxiliary>         m_Auxiliary; // Indexed by view - m_ViewCount.
        uint32_t                       m_Jitter      = 0; // The jitter one stage asks for.
        bool                           m_NeedsBuild  = false;
        GpuScene*                      m_Scene       = nullptr; // Of the current graph.
        Environment*                   m_Environment = nullptr;
    };
} // namespace vultra
