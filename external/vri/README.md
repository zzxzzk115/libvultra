# VRI dependency

The library builds the tagged [VRI v0.2.0](https://github.com/zzxzzk115/VRI/releases/tag/v0.2.0) release, commit
[`6b94de07b7dc2199472d11647424d91fc9f96e99`](https://github.com/zzxzzk115/VRI/tree/v0.2.0), through the shared
xmake `vri` recipe. The recipe and the archive checksum live in the `backup` branch of
[zzxzzk115/xmake-repo](https://github.com/zzxzzk115/xmake-repo); the archive SHA-256 is
`b3f268b4505eb8624e66b7ef3ae30a8b28cc60ca455e625d269176b849800695`. No local patches are applied.
VRI is MIT licensed; its [license](LICENSE) is retained. Backend selection and static linkage remain explicit.

The fixes this directory used to carry as patches are part of that release: validation extent forwarding,
`imageCubeArray`, texture-copy regions, calibrated timestamp entry points and XR pipeline-cache control on Vulkan;
sampler mips, presentation, compute-to-copy ordering and copy-row alignment on WebGPU and WebGL2; x86_64 on Android.
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
