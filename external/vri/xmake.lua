package("vri-vultra")
    set_base("vri")
    add_configs("acceleration_scratch_rebuild", {
        description = "Reacquire released native AS build scratch with recording failure propagation",
        default = false, type = "boolean"
    })
    if is_plat("android") then
        add_configs("android_array_resolve_dependency", {
            description = "Order Qualcomm multisample array resolves", default = false, type = "boolean"
        })
    end
    add_configs("texture_view_formats", {
        description = "Expose explicit native texture-view format compatibility",
        default = false, type = "boolean"
    })
    add_configs("depth_stencil_clear_ranges", {
        description = "Expose selected depth/stencil mip/layer/aspect clears",
        default = false, type = "boolean"
    })
    add_configs("buffer_range_barriers", {
        description = "Expose byte-range buffer barriers with immediate result reporting",
        default = false, type = "boolean"
    })
    add_configs("alias_memory_barrier", {
        description = "Order distinct alias resource accesses with explicit native memory dependencies",
        default = false, type = "boolean"
    })
    add_configs("explicit_memory_contract", {
        description = "Validate placed memory bindings and preserve explicit allocation mapping",
        default = false, type = "boolean"
    })
    add_configs("storage_texture_ranges", {description = "Expose selected mip/layer storage texture clears", default = false, type = "boolean"})
    add_configs("metal_texture_plane_transfers", {description = "Transfer selected Metal depth/stencil buffer planes", default = false, type = "boolean"})
    add_configs("moltenvk_texture_plane_copy", {description = "Preserve MoltenVK depth/stencil copy planes", default = false, type = "boolean"})
    add_configs("metal_texture_plane_copy", {description = "Preserve Metal depth/stencil texture copy planes", default = false, type = "boolean"})
    add_configs("texture_aspect_states", {description = "Expose independent depth/stencil layouts and plane transfers", default = false, type = "boolean"})
    add_configs("surface_format_contract", {description = "Report unsupported Vulkan surface formats", default = false, type = "boolean"})
    add_configs("surface_composite_alpha", {description = "Select a supported Vulkan surface composite alpha", default = false, type = "boolean"})
    add_configs("astc_formats", {description = "Expose ASTC 4x4 and 6x6 formats", default = false, type = "boolean"})
    add_configs("bc_srgb_features", {description = "Expose BC sRGB formats", default = false, type = "boolean"})
    add_configs("macos_bindless_limits", {description = "Apply the MoltenVK bindless capacity fix", default = false, type = "boolean"})
    add_configs("metal_ray_wave_features", {description = "Apply Metal ray query and wave capability fixes", default = false, type = "boolean"})
    add_configs("metal_ray_instance_id", {description = "Preserve Metal ray query instance IDs", default = false, type = "boolean"})
    add_configs("metal_openxr_interop", {description = "Apply Metal native interoperability support", default = false, type = "boolean"})
    add_configs("metal_argument_layout", {
        description = "Preserve sparse Metal argument-buffer descriptor slots", default = false, type = "boolean"
    })
    add_configs("metal_buffer_dimensions", {
        description = "Bind native Metal buffer-view sizes for SPIR-V length queries", default = false, type = "boolean"
    })
    add_configs("metal_compute_state", {
        description = "Preserve Metal compute bindings across encoder boundaries", default = false, type = "boolean"
    })
    add_configs("metal_command_allocator", {
        description = "Own and discard Metal allocator command buffers", default = false, type = "boolean"
    })
    add_configs("metal_command_autorelease", {
        description = "Scope Metal command buffer autorelease ownership", default = false, type = "boolean"
    })
    add_configs("vulkan_storage_features", {
        description = "Enable queried Vulkan storage-image shader capabilities", default = false, type = "boolean"
    })
    add_configs("vulkan_device_contract", {
        description = "Cache key for the patched Vulkan device and adapter contract", default = false, type = "boolean"
    })
    add_configs("vulkan_float16_features", {
        description = "Enable queried Vulkan binary16 shader arithmetic and storage", default = false, type = "boolean"
    })
    add_configs("vulkan_stencil_attachments", {
        description = "Honor explicit Vulkan stencil attachment operations", default = false, type = "boolean"
    })
    add_configs("android_depth_array_clear", {
        description = "Preserve Qualcomm depth array clears within one command buffer", default = false, type = "boolean"
    })
    add_configs("vulkan_portability_features", {
        description = "Enable supported MoltenVK multisample array images", default = false, type = "boolean"
    })
    add_configs("metal_texture_dimensions", {
        description = "Reject unsupported native Metal 1D mip chains before allocation", default = false, type = "boolean"
    })
    add_configs("metal_texture_views", {
        description = "Honor Metal stencil aspects and multisample array views", default = false, type = "boolean"
    })
    add_configs("metal_texture_clear", {
        description = "Implement typed Metal storage texture clears and native format capabilities", default = false, type = "boolean"
    })
    add_configs("command_allocator_ownership", {
        description = "Own validation command buffers in their allocator", default = false, type = "boolean"
    })
    add_configs("storage_buffer_arrays", {
        description = "Expose enabled storage-buffer array indexing and limits", default = false, type = "boolean"
    })
    add_configs("readonly_depth", {description = "Expose preserving read-only depth attachments", default = false, type = "boolean"})
    add_configs("depth_load_contract", {
        description = "Honor Vulkan depth attachment DontCare loads", default = false, type = "boolean"
    })
    add_configs("format_filtering", {
        description = "Expose per-format linear-filter capability", default = false, type = "boolean"
    })
    add_configs("descriptor_binding_flags", {
        description = "Match Vulkan descriptor binding flags to enabled per-type features", default = false, type = "boolean"
    })
    add_configs("color_input_attachments", {
        description = "Expose Vulkan framebuffer-local color input attachments",
        default = false, type = "boolean"
    })
    add_configs("depth_stencil_sample_zero_resolve", {
        description = "Expose native depth and stencil attachment sample-zero resolve",
        default = false, type = "boolean"
    })
    add_configs("vulkan_compute_queue_family", {
        description = "Select and query an explicit separate Vulkan compute queue family",
        default = false, type = "boolean"
    })
    add_configs("vulkan_family_transfer_pair", {
        description = "Expose paired Vulkan resource ownership barriers",
        default = false, type = "boolean"
    })
    add_configs("checked_command_allocator_reset", {
        description = "Report native command allocator reset results",
        default = false, type = "boolean"
    })
    set_urls("https://github.com/zzxzzk115/VRI/archive/d0c78964cc50788eb49d4b7daf967ef3222d2530.tar.gz")
    add_versions("v0.2.0", "b236bd95ad4a9f4f04ffbbf61cbff02934cdcfeac740963e8dd8f2c1025a62ad")
    add_patches("v0.2.0", path.join(os.scriptdir(), "storage_buffer_arrays.patch"),
                "a2d7f268f4a30f584962503efb2f1324392ced9ff8e20abbec81d1f64d26729e")
    add_patches("v0.2.0", path.join(os.scriptdir(), "readonly_depth.patch"),
                "535f5db491ae2c3097bf4ac9efb15c8f5e5accc3ea7eda0cfd73899269784c48")
    add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_depth_load.patch"),
                "eb9fe44719e15f50f5c06c53a6d613c68df7979355ae86ec1713a6c25f6d5ca6")
    add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_stencil_attachments.patch"),
                "89a13dc8c3df3da15e42b731a356f24d3f3cb376c285353589321edb05f13a21")
    add_patches("v0.2.0", path.join(os.scriptdir(), "linear_filter_support.patch"),
                "6a6bd396ddeabfdb5d239bb436656d9a33e7f266ede73a8086133fcb8afb83a7")
    add_patches("v0.2.0", path.join(os.scriptdir(), "validation_extent.patch"),
                "defa9f682a859c8d443bd59861adb0a78a7e0d43d183b7b5d28491ecf039e584")
    add_patches("v0.2.0", path.join(os.scriptdir(), "command_allocator_validation.patch"),
                "89db16c281b13e1103dc69120e87073a7189da3d495fbe2555c8d5c9d1af2768")
    add_patches("v0.2.0", path.join(os.scriptdir(), "cube_array.patch"),
                "1feddcfbeedccc258857d3144524d85d444b96f2c1e26cd2ecfe70ea4d56e7f9")
    add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_swapchain_format.patch"),
                "cede43243b104dfe7bd10470c9f7d3cae63d6c02aa107e542723e751916173fc")
    add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_composite_alpha.patch"),
                "f43592cd1486b1eded23cbc76410a26d993001e9bd3bc8ba1b3687c6d4a6ee28")
    add_patches("v0.2.0", path.join(os.scriptdir(), "texture_copy_region.patch"),
                "3978dea6962f488ac162b44e23b8b5a99f766d7e3c69b8502a994e661b1a8b94")
    add_patches("v0.2.0", path.join(os.scriptdir(), "bc_srgb.patch"),
                "5d4133b27cb4857f9cd73a310775d7ea1aeab860cf668d996c79c7835310356e")
    add_patches("v0.2.0", path.join(os.scriptdir(), "astc_formats.patch"),
                "b7a3ff1d9f92c0d73f7e9cfb241b1007b6658d0c3bcbf90e9b9e1ff28903ac1e")
    if is_plat("macosx") then
        set_license("MIT AND (Apache-2.0 WITH LLVM-exception)")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_argument_padding.patch"),
                    "e378dbf15594822627142a2e2ba9d6431f0b66fc3ad27c53836224028e413115")
        add_patches("v0.2.0", path.join(os.scriptdir(), "macos_bindless_limits.patch"),
                    "929ac3fbe06cf3fec584ae9e38329ed139c8b70e8f7e8244023d90a530cedee5")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_ray_wave_features.patch"),
                    "4b56aae163c7378a48d3816646c27b9847ee306fb9c95d0f1505c54948e5f150")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_ray_instance_id.patch"),
                    "d0df605514797d4a3a2201c4fa40aa905e8b062a2673af656e33acd78b27877c")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_openxr_interop.patch"),
                    "146d5df1caa3108e773f27cde9d45b2f299fd47e45c78421573cb31aa399befb")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_compute_state.patch"),
                    "34e969b2ffa8b0877af8e2e547a1dac690f683ff3bdc0a68a70b1de19f4d8c1a")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_command_allocator.patch"),
                    "56f223f35d771ec6f115a7a3d03929377105d9300580a7cb136e1b68d82e2d04")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_command_autorelease.patch"),
                    "a2beb6fa1b9083ea7097079666df1c1c28e2ce48e44b726595c0424576f7e3c5")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_texture_clear.patch"),
                    "2d1c2e79e608f7229631ea9bd2df7acd6c9fe749321e4fd56f6388bf4ed67523")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_texture_views.patch"),
                    "b364576d669bc59e86300d6f0075e4ebc2864b2f8198c93cc9b5b9e48d6c3c5d")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_texture_dimensions.patch"),
                    "d90b9f69a35f713e870804d4751d7f908f8142834869729fac6a6469bc9abe78")
        add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_portability_features.patch"),
                    "3c3e9cf83f54de6a0c0cecfce0402613b370936326d83cae3f5bc4ec7fc4ba37")
    end
    if is_plat("wasm") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "gl_sampler_mips.patch"),
                    "488f46e77111e344d1aa71396574e4ead7cac1f1306844d40139d721252c4cd4")
        add_patches("v0.2.0", path.join(os.scriptdir(), "web_presentation.patch"),
                    "27c530aa9213dcc1cd267505ad9949377eb273304dceb3813651765cf22ca580")
        add_patches("v0.2.0", path.join(os.scriptdir(), "webgpu_compute_transfer.patch"),
                    "f6b1609e564a5439e5585ec6dd402accd2eef2d92bbfa6212fecad77d26020ba")
        add_patches("v0.2.0", path.join(os.scriptdir(), "webgpu_row_alignment.patch"),
                    "158e04e6c75c7a4d45a93080f7c1e952c832e15d3c939ffe0c80feaa31a19c69")
    end
    if is_plat("android") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "android_depth_array_clear.patch"),
                    "1029c0bd277e8603eb0a8029ec4c222ddbe6388bcc852498d761c8eb52b8cc07")
        add_patches("v0.2.0", path.join(os.scriptdir(), "android_draw_parameters.patch"),
                    "0533e1f25da17bf3b541d4f1629d0de3770460e9648d68f1a13cf05a3ed661a7")
    end
    if is_plat("android") and is_arch("x86_64") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "android_archs.patch"),
                    "ff986707c11605c8635bbbbebb2f776c6956c207087f411cbff491f3ede70cd2")
    end
    add_patches("v0.2.0", path.join(os.scriptdir(), "calibrated_timestamps.patch"),
                "121c78388036374a4a1b141c7238bb0085abf1f4987d53f482dd5736c4e3df9d")
    add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_xr_cache_control.patch"),
                "137f3bb88c403da1c2a3d9de21e222cd54cb99bed42e771f4079899a2a357c9c")
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "moltenvk_adapter_enumeration.patch"),
                    "833a97018004b828f67f9e0dfbf26ef0ac9b432782025a75304a914d410620dd")
    end
    add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_storage_features.patch"),
                "f6a3da0698040e2f1e0417df4ae1ba4b76bc5c94abb02831e07771b76a6d2236")
    add_patches("v0.2.0", path.join(os.scriptdir(), "texture_aspect_states.patch"),
                "ba218cb29a1b35e9c1ebffc443d47e05e49a1ab89d6e29e820af71cd534ccaca")
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_texture_plane_transfers.patch"),
                    "927f7f179db85ae1d78c2706e2997ebe9ce0eb29fce389359da558b9882b5388")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_texture_plane_copy.patch"),
                    "27807f99d1c038fba608a30a6b59d28d6197c4470323a8e4330351a30b824571")
    end
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "moltenvk_texture_plane_copy.patch"),
                    "a5f02e61ee61699f9c0d354640a94bed7b0e6d391dc4b3794b12651d0e8b9f31")
    end
    add_patches("v0.2.0", path.join(os.scriptdir(), "texture_clear_ranges.patch"),
                "cdb1aa755f640ee5274990b9d5f6829c99221bcc16633c11f52027bfa820704a")
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_texture_clear_ranges.patch"),
                    "9d85c2b3828507a2decfbdb239f2e109395f62b64eee25e6ab8f2d109c4c5227")
    end
    add_patches("v0.2.0", path.join(os.scriptdir(), "explicit_memory_contract.patch"),
                "55b833d8d17e608dba02cb1e7565ae0018aacabce614cd9e9268ced45a5e72a4")
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_memory_offsets.patch"),
                    "b72aadd67eef75f2791bb4c5322beb98aade5d79c41f91db9da051bcbf0931cd")
    end
    add_patches("v0.2.0", path.join(os.scriptdir(), "alias_memory_barrier.patch"),
                "d08321eb3ebe99b8a84da79c8ec8ce96f9812c665ffad63ddbeb5900241a6397")
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_alias_memory_barrier.patch"),
                    "53feea95f4be1a7a24198ba731c5df3d0fdef56db9b03bc3ca12238c3348a5af")
    end
    add_patches("v0.2.0", path.join(os.scriptdir(), "buffer_range_barriers.patch"),
                "f6cda202f9f23fb476c5c2c85418caec6d78445525f7f7a5eaccf826f9452c32")
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_buffer_range_barriers.patch"),
                    "decfe1383fa13c06ade98e4fbd010132d5864b1d2beaac71e2c72fcea6c08531")
    end
    add_patches("v0.2.0", path.join(os.scriptdir(), "depth_stencil_clear_ranges.patch"),
                "9d8666211dac42ffb9df49921aea36d2e5dec4aedb0f33a490f289ed0f2b2626")
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_depth_stencil_clear_ranges.patch"),
                    "253d0ef41ebb96b41b82fa1fcb801aabcc66f07d56eb2a70afad4cf8f81fcf28")
    end
    add_patches("v0.2.0", path.join(os.scriptdir(), "mutable_texture_formats.patch"),
                "d1d68876c0a9705103904e040a1a5a65eb91eb7f21a332b6e260afca6b09c158")
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_mutable_texture_formats.patch"),
                    "85de3b5fe87094fb28e0e6d892ecee0133a4bee6b155c55852eb8e29f5dfda4b")
    end
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_buffer_dimensions.patch"),
                    "9fd64974dcac5930fe30e1ef10bbe776703d5e5dff3179e56faddeb4c8e65a80")
    end
    add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_graphics_bindings.patch"),
                "6998b1c4725cab47e11d5d223321e232e38058cea13809f7bf9b8dc870deed72")
    add_patches("v0.2.0", path.join(os.scriptdir(), "descriptor_binding_flags.patch"),
                "e52c16ee52b9b5dd1c8a2a97f63a21fdb25a161771583a5493e6cfc468acaefc")
    if is_plat("android") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "android_array_resolve_dependency.patch"),
                    "c3aafa181d9b5998c23c9a9f995b2f2b8c7910c50e27246a57d779a4145bfa91")
    end
    add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_color_input.patch"),
                "038eed84f72c0eb6c8da1ef6e1cfc2ac6282886485bc2c38ca1dcc5f821d4476")
    add_patches("v0.2.0", path.join(os.scriptdir(), "depth_attachment_resolve.patch"),
                "5313fc3162f2f5834251e263b6bb4fc3a228ae65923eeeb4667fcff29fcda3d5")
    add_patches("v0.2.0", path.join(os.scriptdir(), "stencil_attachment_resolve.patch"),
                "4d5fa1cf387a53575debae3053d2a7ec3af01d0e0a8fb1776fee7011ff529151")
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_depth_attachment_resolve.patch"),
                    "1f2e1c95b03b4fedf9d500acd6b279ce0bb00344559d25031797e2fce51b8493")
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_stencil_attachment_resolve.patch"),
                    "62fbe6d23165c39aa4cfbd77e7169040657b5a99ca24038796db054d98095d3e")
    end

    add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_compute_queue_family.patch"),
                "fc7fa7a04be0d3963e352315a0ddb1934a8117f41cfd808e8b0f92d3c5141078")
    add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_queue_family_transfer.patch"),
                "2e5c0f97c495e8a233d9c2fbdb00a56a72fc11b07c01ccb9dc5c79118e5a5bfc")
    add_patches("v0.2.0", path.join(os.scriptdir(), "command_allocator_checked_reset.patch"),
                "c0db236f72718b8206c15fa7b59fc013052a62aa0acae0c619ccc079eb4f69a6")
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_command_allocator_checked_reset.patch"),
                    "f5b47d049f3dcc3fad90edc22c73006b23cb80ff06c156a7fe6dfbce2b753c1f")
    end

    add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_float16_features.patch"),
                "9fc00c2899046b70b0efe083fad2e58545a25565f5195b321bfb7598cce86600")

    if not is_plat("macosx") then
        -- macOS already retains native recording failures through the MoltenVK plane-copy patch.
        add_patches("v0.2.0", path.join(os.scriptdir(), "vulkan_recording_result.patch"),
                    "07459d2b1c63bb8843a92192a2c7f7624d2da00af1fc7b9292dbab7dc19d1717")
    end
    add_patches("v0.2.0", path.join(os.scriptdir(), "acceleration_scratch_rebuild.patch"),
                "46b8046b042907e996cfb30660cf2f821232433999c80878567bd37df0333304")
    if is_plat("macosx") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "metal_acceleration_scratch.patch"),
                    "0476c4a963d192738889b27eb4f18bb4f23c1b2b2e77ee0535410e7863aa5c43")
    end

package_end()
