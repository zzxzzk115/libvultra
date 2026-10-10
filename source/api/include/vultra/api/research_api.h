#pragma once

#include <vultra/api/research_editor_api.h>
#include <vultra/api/vultra_abi.generated.h>

#include <vri/vri.h>

#ifdef __cplusplus
extern "C"
{
#endif
    /* Borrowed host objects. Extensions must use the SDK exported by their host build. */
    typedef struct VultraGraphFrame
    {
        void*    context;
        uint64_t serial;
    } VultraGraphFrame;

    typedef struct VultraGraphResource
    {
        const void* graph;
        uint32_t    index;
    } VultraGraphResource;

    typedef enum VultraGraphUsage
    {
        VULTRA_COLOR_WRITE,
        VULTRA_COLOR_READ_WRITE,
        VULTRA_DEPTH_WRITE,
        VULTRA_DEPTH_READ,
        VULTRA_DEPTH_READ_WRITE,
        VULTRA_SAMPLED,
        VULTRA_STORAGE_READ,
        VULTRA_STORAGE_WRITE,
        VULTRA_STORAGE_READ_WRITE,
        VULTRA_COPY_SOURCE,
        VULTRA_COPY_DESTINATION,
        VULTRA_PRESENT
    } VultraGraphUsage;

    typedef struct VultraGraphUse
    {
        VultraGraphResource resource;
        VultraGraphUsage    usage;
    } VultraGraphUse;

    typedef struct VultraResearchFrame
    {
        uint64_t index;
        /* Source, left, right. Column-major matrices; zero-to-one projection depth. */
        float view[3][16];
        float projection[3][16];
        float near_plane[3];
        float far_plane[3];
    } VultraResearchFrame;

    typedef struct VultraGraphResourceInfo
    {
        uint8_t        is_texture;
        VriTextureDesc texture;
        VriBufferDesc  buffer;
    } VultraGraphResourceInfo;

    typedef VultraStatus (*VultraGraphExecute)(void*, VultraGraphFrame, VriCommandBuffer*);
    typedef VriPipeline* (*VultraPipelineBuilder)(void*, const VriShaderDesc*, uint32_t);

    typedef struct VultraShaderEntry
    {
        const char*        name;
        VriShaderStageBits stage;
    } VultraShaderEntry;

    typedef struct VultraNativePass
    {
        void* user_data;
        /* Input and parameter arrays survive until graph destruction. Write outputs during build. */
        VultraStatus (*build)(void*,
                              VultraGraphFrame,
                              const char*,
                              const VultraGraphResource*,
                              uint32_t,
                              const double*,
                              uint32_t,
                              VultraGraphResource*,
                              uint32_t);
        void (*stop)(void*);
    } VultraNativePass;

    typedef struct VultraPassPort
    {
        const char* name;
        uint8_t     is_texture;
        VriFormat   format;
        int32_t     same_extent_as_input; /* -1 disables the extent constraint. */
    } VultraPassPort;

    typedef enum VultraPassControl
    {
        VULTRA_PASS_SLIDER,
        VULTRA_PASS_CHECKBOX,
        VULTRA_PASS_CHOICE,
        VULTRA_PASS_READ_ONLY
    } VultraPassControl;

    typedef struct VultraPassChoice
    {
        const char* label;
        double      value;
    } VultraPassChoice;

    typedef struct VultraPassParameterUi
    {
        const char*             label;
        const char*             description;
        VultraPassControl       control;
        const VultraPassChoice* choices;
        uint32_t                choice_count;
        uint8_t                 shared; /* Edit matching parameters across this method's instances of the type. */
    } VultraPassParameterUi;

    typedef struct VultraPassParameter
    {
        const char*                  name;
        double                       value;
        double                       minimum;
        double                       maximum;
        const VultraPassParameterUi* ui; /* Optional presentation metadata; numeric values remain the graph contract. */
    } VultraPassParameter;

    typedef struct VultraNativePassDefinition
    {
        const char*                type;
        const VultraPassPort*      inputs;
        uint32_t                   input_count;
        const VultraPassPort*      outputs;
        uint32_t                   output_count;
        const VultraPassParameter* parameters;
        uint32_t                   parameter_count;
        uint64_t                   required_features;
        void*                      user_data;
        VultraStatus (*create)(void*, VultraNativePass*);
        const char* display_name;
        const char* description;
    } VultraNativePassDefinition;

    typedef struct VultraResearchApi
    {
        uint32_t                version;
        uint32_t                struct_size;
        void*                   context;
        VriDevice*              device;
        const VriCoreInterface* core;
        uint32_t                core_size;
        VriPipelineCache*       pipeline_cache;
        VriResult (*get_interface)(void*, const char*, size_t, void*);
        /* Metadata is copied; registration ends after module initialization. */
        VultraStatus (*register_pass)(void*, const VultraNativePassDefinition*);
        VultraStatus (*resource_info)(VultraGraphFrame, VultraGraphResource, VultraGraphResourceInfo*);
        VultraStatus (*create_texture)(VultraGraphFrame, const char*, const VriTextureDesc*, VultraGraphResource*);
        VultraStatus (*create_buffer)(VultraGraphFrame, const char*, const VriBufferDesc*, VultraGraphResource*);
        VultraStatus (*add_pass)(VultraGraphFrame,
                                 const char*,
                                 const VultraGraphUse*,
                                 uint32_t,
                                 VultraGraphExecute,
                                 void*,
                                 uint8_t);
        VultraStatus (*texture)(VultraGraphFrame, VultraGraphResource, VriTexture**, VriDescriptor**);
        VultraStatus (*buffer)(VultraGraphFrame, VultraGraphResource, VriBuffer**);
        /* Read during command execution, never freeze camera matrices during graph construction. */
        VultraStatus (*frame)(VultraGraphFrame, VultraResearchFrame*);
        /* Shader paths are project-relative declared assets. VPKs supply cooked siblings. */
        VultraStatus (*create_shader)(void*,
                                      const char*,
                                      const VultraShaderEntry*,
                                      uint32_t,
                                      VultraPipelineBuilder,
                                      void*,
                                      void**);
        VriPipeline* (*pipeline)(void*, void*);
        VultraStatus (*destroy_shader)(void*, void*);
        void (*log)(void*, const char*);
        uint32_t pass_definition_size; /* Check with struct_size before registering module metadata. */
        VultraStatus (*register_editor)(void*, const char*); /* Project-owned Controls content; init only. */
        const VultraResearchEditorApi* editor;
    } VultraResearchApi;
#ifdef __cplusplus
}
#endif
