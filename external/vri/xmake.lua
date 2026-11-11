-- A local checkout is for coordinated VRI development; it uses that source as-is.
-- A separate name keeps a local build from reusing an installed release build.
local vri_dir = get_config("libvultra_vri_dir")
if vri_dir and vri_dir ~= "" then
    package("vri-vultra-local")
        set_base("vri")
        set_sourcedir(path.absolute(vri_dir, os.projectdir()))
    package_end()
end
