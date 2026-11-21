// Two event rows share the ID reduction, timestamp stores and validation branch.
// The assembly calls C++ for purchase/repair/flush; the unwind frame preserves
// normal exception handling if a sink requests the generic pipeline.
#if defined(__x86_64__) && defined(__ELF__) && defined(SHOP_PAIRED_EVENT_KERNEL) && defined(SHOP_KERNEL_ABSOLUTE_TABLES)
.intel_syntax noprefix
.text
.hidden shopping_kernel_event_vectors
.hidden shopping_kernel_event_id_rows
.hidden shopping_kernel_event_kinds
.hidden shopping_kernel_event_clocks
.hidden shopping_kernel_event_tails
.hidden shopping_kernel_event_redo
.hidden shopping_kernel_event_flush
.hidden shopping_kernel_event_purchase

.macro ev_consts
    vmovdqa ymm15, ymmword ptr [rip + shopping_kernel_event_vectors + 224]
    vmovdqa ymm14, ymmword ptr [rip + shopping_kernel_event_vectors + 256]
    vmovdqa ymm13, ymmword ptr [rip + shopping_kernel_event_vectors + 288]
    vmovdqa64 ymm16, ymmword ptr [rip + shopping_kernel_event_vectors + 352]
    vmovdqa64 ymm24, ymmword ptr [rip + shopping_kernel_event_vectors + 0]
    vmovdqa64 ymm25, ymmword ptr [rip + shopping_kernel_event_vectors + 32]
    vmovdqa64 ymm26, ymmword ptr [rip + shopping_kernel_event_vectors + 64]
    vmovdqa64 ymm28, ymmword ptr [rip + shopping_kernel_event_vectors + 320]
    xor r11d, r11d
    mov eax, 0xffe
    kmovd k5, eax
    mov eax, 12
    kmovd k1, eax
.endm

.macro ev_save_a
    vmovdqu ymmword ptr [rsp + 448], ymm12
    vmovdqu ymmword ptr [rsp + 64], ymm9
    vmovdqu ymmword ptr [rsp + 96], ymm8
    vmovdqu64 ymmword ptr [rsp + 256], ymm30
    vmovdqu64 ymmword ptr [rsp + 288], ymm20
    vmovdqu64 ymmword ptr [rsp + 320], ymm21
    vmovdqu64 ymmword ptr [rsp + 352], ymm22
    vmovdqu64 ymmword ptr [rsp + 384], ymm23
    vmovdqu64 ymmword ptr [rsp + 416], ymm31
.endm

.macro ev_restore_a
    vmovdqu ymm12, ymmword ptr [rsp + 448]
    vmovdqu ymm9, ymmword ptr [rsp + 64]
    vmovdqu ymm8, ymmword ptr [rsp + 96]
    vmovdqu64 ymm30, ymmword ptr [rsp + 256]
    vmovdqu64 ymm20, ymmword ptr [rsp + 288]
    vmovdqu64 ymm21, ymmword ptr [rsp + 320]
    vmovdqu64 ymm22, ymmword ptr [rsp + 352]
    vmovdqu64 ymm23, ymmword ptr [rsp + 384]
    vmovdqu64 ymm31, ymmword ptr [rsp + 416]
    ev_consts
.endm

.macro ev_save_b
    mov qword ptr [rsp + 40], rcx
    vmovdqu ymmword ptr [rsp + 448], ymm12
    vmovdqu ymmword ptr [rsp + 64], ymm11
    vmovdqu ymmword ptr [rsp + 96], ymm10
    vmovdqu64 ymmword ptr [rsp + 128], ymm27
    vmovdqu64 ymmword ptr [rsp + 160], ymm29
    vmovdqu64 ymmword ptr [rsp + 192], ymm17
    vmovdqu64 ymmword ptr [rsp + 224], ymm18
    vmovdqu64 ymmword ptr [rsp + 352], ymm22
    vmovdqu64 ymmword ptr [rsp + 384], ymm23
.endm

.macro ev_restore_b
    mov rcx, qword ptr [rsp + 40]
    vmovdqu ymm12, ymmword ptr [rsp + 448]
    vmovdqu ymm11, ymmword ptr [rsp + 64]
    vmovdqu ymm10, ymmword ptr [rsp + 96]
    vmovdqu64 ymm27, ymmword ptr [rsp + 128]
    vmovdqu64 ymm29, ymmword ptr [rsp + 160]
    vmovdqu64 ymm17, ymmword ptr [rsp + 192]
    vmovdqu64 ymm18, ymmword ptr [rsp + 224]
    vmovdqu64 ymm22, ymmword ptr [rsp + 352]
    vmovdqu64 ymm23, ymmword ptr [rsp + 384]
    ev_consts
.endm

.macro ev_next
    vpcmpeqb k0, ymm14, ymmword ptr [rdi + 32]
    vpcmpeqb k6, ymm14, ymmword ptr [rdi + 64]
    kunpckdq k0, k6, k0
    kmovq rax, k0
    tzcnt rax, rax
    lea rdi, [rdi + rax + 33]
.endm

.macro ev_mk a, b, cl, ch
    vpmovmskb eax, ymm12
    tzcnt eax, eax
    lea \a\(), [\b\() + rax + 65]
    vpcmpeqb ymm12, ymm14, ymmword ptr [\a\() + 64]
    vpcmpeqb \cl\(), ymm15, ymmword ptr [\a\()]
    vpcmpeqb \ch\(), ymm15, ymmword ptr [\a\() + 32]
    prefetcht0 byte ptr [\a\() + 1024]
.endm

.macro ev_dec jo, x, d, b2, all, r
    vpshufb ymm0, \x\(), ymmword ptr [shopping_kernel_event_clocks + \jo\() + 32768]
    vpsubb ymm1, ymm0, ymmword ptr [shopping_kernel_event_clocks + \jo\() + 32800]
    vpmaddubsw ymm0, ymm1, ymmword ptr [shopping_kernel_event_clocks + \jo\() + 32864]
    vpmaddwd ymm2, ymm0, ymmword ptr [shopping_kernel_event_clocks + \jo\() + 32896]
    vpackusdw ymm0, ymm2, ymm2
    vpmaddwd ymm0, ymm0, ymmword ptr [shopping_kernel_event_clocks + \jo\() + 32928]
    vpsubusb \all\(), ymm1, ymmword ptr [shopping_kernel_event_clocks + \jo\() + 32832]
    vpcmpgtd ymm2, ymm2, ymmword ptr [shopping_kernel_event_clocks + \jo\() + 32960]
    vpmuludq ymm5, ymm0, ymmword ptr [shopping_kernel_event_clocks + \jo\() + 33024]
    vpsrlq ymm0, ymm0, 32
    vpsignd ymm0, ymm0, ymm1
    vpmuldq ymm0, ymm0, ymmword ptr [shopping_kernel_event_clocks + \jo\() + 33056]
    vpaddq ymm5, ymm5, \d\()
    vpaddq \r\(), ymm5, ymm0
    vpternlogq \all\(), ymm2, \b2\(), 0xfe
.endm

.macro ev_hd b, cl, ch
    vpmovmskb ecx, \cl\()
    tzcnt r8d, ecx
    blsr ecx, ecx
    tzcnt r9d, ecx
    vpmovmskb edx, \ch\()
    tzcnt edx, edx
    lea rsi, [\b\() + rdx + 32]
    sub rdx, r9
.endm

.macro ev_st b, jo, x, d, b2, b2x, v, ids, fail, plab, pret, ppst
    cmp eax, 31
    ja \fail\()f
    vmovdqu ymm3, ymmword ptr [\b\() + r8 - 16]
    shl r8, 6
    add r8, r9
    shl r8, 7
    vpshufb ymm3, ymm3, ymmword ptr [shopping_kernel_event_id_rows + r8]
    vpsubb \v\(), ymm3, ymmword ptr [shopping_kernel_event_id_rows + r8 + 32]
    vpaddb ymm4, \v\(), ymm24
    vpsubusb ymm4, ymm4, ymm26
    vpsubusb ymm5, \v\(), ymm25
    vpminub ymm5, ymm5, ymm4
    .if \ids
    vpunpckhqdq ymm3, ymm22, ymm3
    vpaddb ymm4, ymm3, ymmword ptr [rip + shopping_kernel_event_vectors + 96]
    vpminub ymm3, ymm3, ymm4
    vpmaddubsw ymm3, ymm3, ymmword ptr [rip + shopping_kernel_event_vectors + 128]
    vpmaddwd ymm3, ymm3, ymmword ptr [rip + shopping_kernel_event_vectors + 160]
    vpmuludq ymm4, ymm3, ymmword ptr [rip + shopping_kernel_event_vectors + 192]
    vpsrlq ymm3, ymm3, 32
    vpaddq ymm3, ymm3, ymm4
    vmovdqu xmmword ptr [r15 + rbx + 1024], xmm3
    vmovdqu64 ymmword ptr [r15 + rbx + 1536 - 16] {k1}, ymm3
    .endif
    cmp byte ptr [rsi + 1], 34
    sete r11b
    lea \jo\(), [r11 + rdx*2]
    shl \jo\(), 9
    lea rdx, [rsi + r11*2]
    vmovdqu ymm6, ymmword ptr [\b\() + rax + 32]
    add rax, \b\()
    vpcmpeqb ymm7, ymm13, ymm6
    vpmovmskb edi, ymm7
    vpcmpeqb ymm1, ymm14, ymm6
    vpcmpeqb ymm6, ymm15, ymm6
    vpmovmskb r10d, ymm6
    lzcnt r9d, r10d
    shlx r10d, r10d, r9d
    add r10d, r10d
    lzcnt r10d, r10d
    sub rax, rdx
    lea r10, [r10 + r10*8]
    lea r9, [r9 + r10*8]
    shl r9, 6
    and edi, dword ptr [shopping_kernel_event_tails + r9 + 0 + 4]
    pext r10d, edi, dword ptr [shopping_kernel_event_tails + r9 + 0]
    mov r8d, dword ptr [shopping_kernel_event_tails + r9 + r10*4 + 0 + 8]
    cmp edi, r8d
    jne \fail\()f
    sub rax, qword ptr [shopping_kernel_event_tails + r9 + 0 + 56]
    movzx edi, byte ptr [rdx + 23]
    shl edi, 6
    mov rcx, qword ptr [shopping_kernel_event_kinds + rdi + 32]
    or rcx, qword ptr [shopping_kernel_event_tails + r9 + r10*8 + 0 + 24]
    vmovdqu xmm6, xmmword ptr [rdx + 21]
    vpxor xmm6, xmm6, xmmword ptr [shopping_kernel_event_kinds + rdi]
    vpand xmm6, xmm6, xmmword ptr [shopping_kernel_event_kinds + rdi + 16]
    mov r8, qword ptr [shopping_kernel_event_clocks + \jo\() + 33144]
    add r8, rsi
    movzx r9d, word ptr [r8 + 9]
    pext r9, r9, qword ptr [shopping_kernel_event_clocks + \jo\() + 33128]
    shl r9d, 5
    vmovdqa ymm2, ymmword ptr [r15 + r9 + 2112]
    vpxor xmm3, xmm2, xmmword ptr [r8]
    vpandq \b2x\(), xmm3, xmmword ptr [shopping_kernel_event_clocks + \jo\() + 32992]
    movzx r10d, word ptr [rsi + r11 + 9]
    pext r10, r10, qword ptr [shopping_kernel_event_clocks + \jo\() + 33136]
    shl r10d, 5
    vmovdqa ymm4, ymmword ptr [r15 + r10 + 2112]
    vpsubb xmm7{k5}{z}, xmm4, xmmword ptr [rsi + r11]
    vpternlogq ymm7, ymm6, ymm5, 0xfe
    vpternlogq \b2\(), ymm7, ymm1, 0xfe
    vmovdqu64 \x\(), ymmword ptr [rsi - 16]
    vpermt2q \x\(), ymm16, ymmword ptr [rdx - 11]
    vshufi64x2 \d\(), ymm2, ymm4, 3
    cmp rax, qword ptr [shopping_kernel_event_kinds + rdi + 40]
    jne \plab\()f
\pret\():
    .if \ppst
    mov qword ptr [r15 + rbx + 512 + 8], rcx
    .endif
.endm

.macro ev_pur plab, pret, fail, a, b, save, restore
\plab\():
    cmp rax, qword ptr [shopping_kernel_event_kinds + rdi + 48]
    jne \fail\()f
    \save
    mov rdi, \b\()
    mov rsi, \a\()
    lea rdx, [rsp + 56]
    vzeroupper
    call shopping_kernel_event_purchase
    mov qword ptr [rsp + 48], rax
    \restore
    mov rcx, qword ptr [rsp + 56]
    cmp byte ptr [rsp + 48], 0
    je \fail\()f
    jmp \pret\()b
.endm

.globl shopping_kernel_event_loop
.hidden shopping_kernel_event_loop
.type shopping_kernel_event_loop,@function
.p2align 5
shopping_kernel_event_loop:
    .cfi_startproc
    push r12
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset r12,0
    push r13
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset r13,0
    push r14
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset r14,0
    push r15
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset r15,0
    push rbx
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset rbx,0
    push rbp
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset rbp,0
    mov rax, rsp
    and rsp, -64
    sub rsp, 512
    mov qword ptr [rsp + 504], rax
    // CFA = *(rsp + 504) + 56, including all six saved registers.
    .cfi_escape 0x0f,0x06,0x77,0xf8,0x03,0x06,0x23,0x38
    mov qword ptr [rsp + 8], rcx
    mov qword ptr [rsp + 24], r8
    mov qword ptr [rsp + 32], r9
    lea r15, [rdx + 512]
    mov rbx, rsi
    sub rbx, r15
    mov rbp, rdi
    mov qword ptr [rsp], rbp
    ev_consts
    vpcmpeqb ymm12, ymm14, ymmword ptr [rbp + 64]
    vpcmpeqb ymm9, ymm15, ymmword ptr [rbp]
    vpcmpeqb ymm8, ymm15, ymmword ptr [rbp + 32]
    ev_hd rbp, ymm9, ymm8
    ev_mk r12, rbp, ymm11, ymm10
    ev_st rbp, r13, ymm29, ymm17, ymm18, xmm18, ymm22, 0, 72, 52, 53, 0
    jmp 22f
    .p2align 5
22:
    mov qword ptr [r15 + rbx + 512], rcx
    ev_hd r12, ymm11, ymm10
    ev_mk rbp, r12, ymm9, ymm8
    ev_dec r13, ymm29, ymm17, ymm18, ymm23, ymm31
    ev_st r12, r14, ymm30, ymm20, ymm21, xmm21, ymm3, 1, 73, 54, 55, 1
    ev_hd rbp, ymm9, ymm8
    ev_mk r12, rbp, ymm11, ymm10
    ev_dec r14, ymm30, ymm20, ymm21, ymm27, ymm4
    vpermt2q ymm31, ymm28, ymm4
    vmovdqu64 xmmword ptr [r15 + rbx], xmm31
    vmovdqu64 ymmword ptr [r15 + rbx + 2048 - 16] {k1}, ymm31
    ev_st rbp, r13, ymm29, ymm17, ymm18, xmm18, ymm22, 0, 74, 56, 57, 0
    vpord ymm3, ymm27, ymm23
    vptest ymm3, ymm3
    jnz 75f
23:
    mov qword ptr [rsp], rbp
    add rbx, 16
    jnz 22b
    ev_save_b
    mov qword ptr [r15 + 4160], 64
    mov rdi, qword ptr [rsp + 32]
    vzeroupper
    call shopping_kernel_event_flush
    ev_restore_b
    mov rbx, -512
    cmp rbp, qword ptr [rsp + 24]
    jbe 22b
    mov rdi, rbp
    lea rsi, [r15 - 512]
    xor eax, eax
    jmp 99f
    ev_pur 52, 53, 72, r12, rbp, ev_save_b, ev_restore_b
    ev_pur 54, 55, 73, rbp, r12, ev_save_a, ev_restore_a
    ev_pur 56, 57, 74, r12, rbp, ev_save_b, ev_restore_b
72:
    mov rdi, rbp
    lea rsi, [r15 + rbx]
    mov eax, 1
    jmp 99f
73:
    vmovdqa64 ymm0, ymm23
    vptest ymm0, ymm0
    jnz 78f
    vpunpckhqdq ymm3, ymm22, ymm22
    vpaddb ymm4, ymm3, ymmword ptr [rip + shopping_kernel_event_vectors + 96]
    vpminub ymm3, ymm3, ymm4
    vpmaddubsw ymm3, ymm3, ymmword ptr [rip + shopping_kernel_event_vectors + 128]
    vpmaddwd ymm3, ymm3, ymmword ptr [rip + shopping_kernel_event_vectors + 160]
    vpmuludq ymm4, ymm3, ymmword ptr [rip + shopping_kernel_event_vectors + 192]
    vpsrlq ymm3, ymm3, 32
    vpaddq ymm3, ymm3, ymm4
    vmovq qword ptr [r15 + rbx + 1024], xmm3
    vextracti128 xmm0, ymm3, 1
    vmovq qword ptr [r15 + rbx + 1536], xmm0
    vmovhps qword ptr [r15 + rbx], xmm31
    vextracti32x4 xmm0, ymm31, 1
    vmovq qword ptr [r15 + rbx + 2048], xmm0
    mov rdi, r12
    lea rsi, [r15 + rbx + 8]
    mov eax, 1
    jmp 99f
78:
    mov rdi, qword ptr [rsp]
    lea rsi, [r15 + rbx]
    mov eax, 1
    jmp 99f
74:
    vmovdqa64 ymm0, ymm23
    vptest ymm0, ymm0
    jnz 78b
    vptestmq k0, ymm27, ymm27
    kortestb k0, k0
    jnz 79f
    mov rdi, rbp
    lea rsi, [r15 + rbx + 16]
    mov eax, 1
    cmp rbx, -16
    jne 99f
    mov qword ptr [r15 + 4160], 64
    mov rdi, qword ptr [rsp + 32]
    vzeroupper
    call shopping_kernel_event_flush
    mov rdi, rbp
    lea rsi, [r15 - 512]
    mov eax, 1
    jmp 99f
79:
    mov rdi, qword ptr [rsp]
    ev_next
    lea rsi, [r15 + rbx + 8]
    mov eax, 1
    jmp 99f
75:
    ev_save_b
    mov rdi, qword ptr [rsp]
    ev_next
    mov qword ptr [rsp + 16], rdi
    vmovdqa64 ymm0, ymm23
    vptest ymm0, ymm0
    jz 82f
    mov rdi, qword ptr [rsp + 8]
    mov rsi, qword ptr [rsp]
    lea rdx, [r15 + rbx]
    vzeroupper
    call shopping_kernel_event_redo
    cmp rax, qword ptr [rsp + 16]
    jne 84f
    lea rcx, [r15 + rbx + 8]
    cmp rdx, rcx
    jne 84f
    mov rdi, qword ptr [rsp]
    vmovdqu ymm0, ymmword ptr [rdi + 64]
    vpcmpeqb ymm0, ymm0, ymmword ptr [rip + shopping_kernel_event_vectors + 256]
    vpmovmskb eax, ymm0
    tzcnt eax, eax
    lea rax, [rdi + rax + 65]
    cmp rax, qword ptr [rsp + 16]
    jne 86f
    vmovdqu64 ymm0, ymmword ptr [rsp + 128]
    vptest ymm0, ymm0
    jz 83f
82:
    mov rdi, qword ptr [rsp + 8]
    mov rsi, qword ptr [rsp + 16]
    lea rdx, [r15 + rbx + 8]
    vzeroupper
    call shopping_kernel_event_redo
    cmp rax, rbp
    jne 84f
    test dl, 8
    jnz 84f
    mov rbx, rdx
    sub rbx, r15
    cmp rbp, qword ptr [rsp + 24]
    ja 85f
    sub rbx, 16
83:
    ev_restore_b
    jmp 23b
84:
    mov rdi, rax
    mov rsi, rdx
    xor eax, eax
    jmp 99f
86:
    mov rdi, qword ptr [rsp + 16]
    mov rsi, rdx
    xor eax, eax
    jmp 99f
85:
    mov rdi, rbp
    lea rsi, [r15 + rbx]
    xor eax, eax
99:
    mov rsp, qword ptr [rsp + 504]
    .cfi_def_cfa rsp,56
    pop rbp
    .cfi_adjust_cfa_offset -8
    .cfi_restore rbp
    pop rbx
    .cfi_adjust_cfa_offset -8
    .cfi_restore rbx
    // The seventh System V argument points at the three-word exit state.
    mov rcx, qword ptr [rsp + 40]
    mov qword ptr [rcx], rax
    mov qword ptr [rcx + 8], rdi
    mov qword ptr [rcx + 16], rsi
    pop r15
    .cfi_adjust_cfa_offset -8
    .cfi_restore r15
    pop r14
    .cfi_adjust_cfa_offset -8
    .cfi_restore r14
    pop r13
    .cfi_adjust_cfa_offset -8
    .cfi_restore r13
    pop r12
    .cfi_adjust_cfa_offset -8
    .cfi_restore r12
    vzeroupper
    ret
    .cfi_endproc
.size shopping_kernel_event_loop, .-shopping_kernel_event_loop
.section .note.GNU-stack,"",@progbits
#endif
