package("vri-vultra")
    set_base("vri")
    set_urls("https://github.com/zzxzzk115/VRI/archive/d0c78964cc50788eb49d4b7daf967ef3222d2530.tar.gz")
    add_versions("v0.2.0", "b236bd95ad4a9f4f04ffbbf61cbff02934cdcfeac740963e8dd8f2c1025a62ad")
    add_patches("v0.2.0", path.join(os.scriptdir(), "validation_extent.patch"),
                "defa9f682a859c8d443bd59861adb0a78a7e0d43d183b7b5d28491ecf039e584")
    add_patches("v0.2.0", path.join(os.scriptdir(), "cube_array.patch"),
                "1feddcfbeedccc258857d3144524d85d444b96f2c1e26cd2ecfe70ea4d56e7f9")
    add_patches("v0.2.0", path.join(os.scriptdir(), "texture_copy_region.patch"),
                "3978dea6962f488ac162b44e23b8b5a99f766d7e3c69b8502a994e661b1a8b94")
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
    if is_plat("android") and is_arch("x86_64") then
        add_patches("v0.2.0", path.join(os.scriptdir(), "android_archs.patch"),
                    "ff986707c11605c8635bbbbebb2f776c6956c207087f411cbff491f3ede70cd2")
    end
package_end()
