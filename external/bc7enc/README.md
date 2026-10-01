# BC7 Encoder

Unmodified `bc7e.ispc` from [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo)
at `b9438627eef73a1157e84201b6fa6eb2ffd6d9f0`.
Copyright Binomial LLC; licensed under [Apache-2.0](LICENSE-APACHE-2.0).

Vultra builds SSE2 and AVX2 implementations with ISPC 1.28.2 and uses its runtime
dispatch. The fast preset encodes linear RGBA directly to BC7; no UASTC intermediate
is produced. The source has no local modifications. Generated objects and headers
remain in the build directory.

The `bc7enc` static target owns ISPC compilation and exports its generated header
through `add_deps("bc7enc")`. Its local rule registers all three dispatch objects;
xmake's generic ISPC rule only registers the main object. A build fence makes the
header available before dependent C++ files compile.
