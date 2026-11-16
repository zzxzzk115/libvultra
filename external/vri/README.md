# VRI dependency

The library builds the tagged [VRI v0.2.2](https://github.com/zzxzzk115/VRI/releases/tag/v0.2.2) release, commit
[`a6b5127816981735775a4afcb811431e2f302844`](https://github.com/zzxzzk115/VRI/tree/v0.2.2), through the shared
xmake `vri` recipe. The recipe and the archive checksum live in the `backup` branch of
[zzxzzk115/xmake-repo](https://github.com/zzxzzk115/xmake-repo); the archive SHA-256 is
`8c96028afc6316bb3d8b599e3f294edd11eb683b77f952642888ec7109ff9237`. No local patches are applied.
VRI is MIT licensed; its [license](LICENSE) is retained. Backend selection and static linkage remain explicit.

The fixes this directory used to carry as patches are part of that release: validation extent forwarding,
`imageCubeArray`, texture-copy regions, calibrated timestamp entry points and XR pipeline-cache control on Vulkan;
sampler mips, presentation, compute-to-copy ordering and copy-row alignment on WebGPU and WebGL2; x86_64 on Android.
v0.2.1 adds the macOS fixes: the Vulkan backend no longer assumes a 1.3 device (MoltenVK reports 1.2), and Metal
reports point and range timestamps separately. v0.2.2 adds the WebGPU fixes (compute, typed storage, copies and
device limits), ETC2 and ASTC 4x4 formats, 64-bit storage-buffer atomics, the identity surface transform on Vulkan
and the Direct3D 12 backend fixes.
Texture, resize, odd-sized transfer and platform readback tests exercise these behaviors; they are not standards
conformance or headset acceptance.

`Device` accepts a VRI creation descriptor directly. Its convenience constructors explicitly select Y-up for the
existing builtin shaders/cameras; VRI's default descriptor still selects Vulkan conventions. Frame submission,
completion and explicit idle waits check their results. Destruction reports idle failure without throwing.

When rebuilding dependencies, preserve the configured version/backend flags: an unqualified package requirement
can select a different package. A changed static package requires relinking its consumers. Browser adapter/device
startup, native Linux WebGPU surfaces and hardware acceptance remain independent platform gates.

For coordinated VRI development, `--libvultra_vri_dir=<checkout>` builds VRI from a local source tree as the
separately named `vri-vultra-local` package.
