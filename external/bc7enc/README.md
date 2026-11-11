# BC7 Encoder

`bc7e.ispc` from [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo)
at `b9438627eef73a1157e84201b6fa6eb2ffd6d9f0`, with one local patch.
Copyright Binomial LLC; licensed under [Apache-2.0](LICENSE-APACHE-2.0).

Vultra builds SSE2 and AVX2 implementations with ISPC 1.28.2 and uses its runtime
dispatch on x86-64; on arm64 it builds a single NEON implementation. The fast preset
encodes linear RGBA directly to BC7; no UASTC intermediate is produced. Generated
objects and headers remain in the build directory.

Local patch: in `encode_bc7_block`, the two anchor-index decrements `n--` are written
as `n = n - 1`. ISPC 1.28.2 drops the masked `n--` on the varying index size, so the
1-bit anchor index was written with 2 bits and every following index shifted by one
bit. Solid-color blocks (mode 5) then decoded with wrong colors on SSE2, AVX2 and NEON
alike. The patch changes no encoding decision.

The `bc7enc` static target owns ISPC compilation and exports its generated header
through `add_deps("bc7enc")`. Its local rule registers the dispatch objects of every
ISA it builds; xmake's generic ISPC rule only registers the main object. A build fence
makes the header available before dependent C++ files compile.
