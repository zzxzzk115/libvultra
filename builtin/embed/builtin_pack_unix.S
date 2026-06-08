// Embeds builtin/generated/builtin.vpk as a rodata blob with start/end symbols.
// .incbin path is relative to the assembler working directory (the project root).
#if defined(__APPLE__)
.section __DATA,__vultra_pack
.globl _vultra_builtin_pack_start
.globl _vultra_builtin_pack_end
.p2align 4
_vultra_builtin_pack_start:
.incbin "builtin/generated/builtin.vpk"
_vultra_builtin_pack_end:
#else
.section .rodata.vultra_pack,"a",@progbits
.globl vultra_builtin_pack_start
.globl vultra_builtin_pack_end
.type vultra_builtin_pack_start, @object
.type vultra_builtin_pack_end, @object
.p2align 4
vultra_builtin_pack_start:
.incbin "builtin/generated/builtin.vpk"
vultra_builtin_pack_end:
#endif
