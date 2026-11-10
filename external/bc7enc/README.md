# BC7 Encoder

Pinned `bc7e.ispc` from [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo)
at [`b9438627eef73a1157e84201b6fa6eb2ffd6d9f0`](https://github.com/richgel999/bc7enc_rdo/tree/b9438627eef73a1157e84201b6fa6eb2ffd6d9f0).
Copyright Binomial LLC; licensed under [Apache-2.0](LICENSE-APACHE-2.0).

Vultra uses ISPC 1.28.2: NEON on ARM64, or SSE2/AVX2 with runtime dispatch on x86-64.
The fast preset encodes linear RGBA directly to BC7; no UASTC intermediate is produced.
Generated objects and headers remain in the build directory.

Local patch: replace the five standalone varying postfix decrements with equivalent explicit assignments.
On the qualified NEON build, the original selector fixup `n--` leaves two bits at an anchor that requires one.
A constant (51,102,204,255) block consequently decodes with green values ranging from 102 to 179. Traced explicit
subtraction restores the intended offset and constant decoded value; independent DirectXTex and native GPU checks
cover the result. This changes neither the algorithm nor quality preset. The other standalone selector/endpoint
decrements use the same explicit form. Unchanged O0 and an alternative anchor-lookup formulation did not repair
the observed encoder. Do not generalize this record to untested ISPC versions or CPU architectures.
The asset import recipe identifies the corrected encoder separately so older encoded cache entries rebuild.

The `bc7enc` static target owns ISPC compilation and exports its generated header through `add_deps("bc7enc")`.
Its local rule registers all dispatch objects; xmake's generic ISPC rule only registers the main object. A build
fence makes the header available before dependent C++ files compile.
