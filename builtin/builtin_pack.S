#if defined(__APPLE__)
.section __DATA,__vultra_builtin_pack
.globl _vultra_builtin_pack_start
.globl _vultra_builtin_pack_end
.p2align 4
_vultra_builtin_pack_start:
.incbin "build/.tmp/runtime-builtin.vpk"
_vultra_builtin_pack_end:
#else
.section .rodata.vultra_builtin_pack,"a",@progbits
.globl vultra_builtin_pack_start
.globl vultra_builtin_pack_end
.type vultra_builtin_pack_start, @object
.p2align 4
vultra_builtin_pack_start:
.incbin "build/.tmp/runtime-builtin.vpk"
vultra_builtin_pack_end:
.section .note.GNU-stack,"",@progbits
#endif
