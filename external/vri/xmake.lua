package("vri-vultra")
    set_base("vri")
    add_patches("v0.1.17", path.join(os.scriptdir(), "validation_extent.patch"),
                "d0cb07178985dec1fa5d9e2fc76ce8805489dfd836c796075a59594fc9084e96")
    add_patches("v0.1.17", path.join(os.scriptdir(), "cube_array.patch"),
                "51ff495f9a58a53a9a2edadc063d5298a705cfa916f9d6b53450f13f9f1986e1")
package_end()
