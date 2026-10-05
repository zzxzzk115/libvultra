# VRI validation-layer patch

Vultra uses [VRI v0.1.17](https://github.com/zzxzzk115/VRI/tree/v0.1.17), licensed under MIT. The upstream source archive is pinned by SHA-256 `b8a2de7b1d52c10d9d42da2147b01c259a5dcc166cb334b712b3bc041b1a5800` in the existing xmake package recipe; its [MIT license](LICENSE), copyright (c) 2026 Lazy_V, is retained here and in the downloaded source.

`validation_extent.patch` is a local fix to `source/core/validation_layer.cpp`. It forwards `GetSwapChainExtent` through the validation wrapper. The Vulkan backend already implements this function, but the wrapper's table omitted it. During asynchronous window resize, falling back to the requested dimensions can create a render area larger than the actual image (VUID 06079/06080).

The local `vri-vultra` package inherits the upstream `vri` recipe and applies the checksum-verified patch. It is exposed to project targets as `vri`; the separate package name prevents reuse of an unpatched installed library. Dependency versions, backend options and validation remain unchanged.

`test-display` exercises resize, GPU readback and swapchain color formats with validation enabled. Remove the local package override when a deliberately selected upstream release includes this forwarding fix.

The checksum-verified `cube_array.patch` enables Vulkan's queried `imageCubeArray` feature when supported. VRI already exposes cube-array textures and views, but v0.1.17 did not enable this required device feature. `test-shader-textures` checks typed array, volume, cube and cube-array GPU sampling with validation. Existing installations of the earlier local patch set must be rebuilt with `xmake require --force --shallow -y vri-vultra`.
