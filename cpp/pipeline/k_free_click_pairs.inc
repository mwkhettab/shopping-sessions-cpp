// Decode click pairs and retry the first row that fails validation.
.intel_syntax noprefix
.text
.hidden shopping_click_campaigns
.hidden shopping_click_id_pairs
.hidden shopping_click_clocks
.hidden shopping_click_constants

.macro click_pair_prepare a, b, cin, nlin, cout, nlout, row, x, days, bad, badx, camp, ids
    lea \a, [\b + \nlin + 33]
    vpcmpeqb ymm0, ymm13, ymmword ptr [\a]
    vpmovmskb \cout, ymm0
    vpcmpeqb ymm1, ymm12, ymmword ptr [\a + 32]
    vpmovmskb \nlout, ymm1
    tzcnt \nlout, \nlout
    prefetcht0 byte ptr [\a + 1024]
    tzcnt r8d, \cin
    blsr \cin, \cin
    tzcnt r9d, \cin
    blsr \cin, \cin
    tzcnt r10d, \cin
    vmovdqu ymm3, ymmword ptr [\b + r8 - 16]
    movzx edx, byte ptr [\b + r9 + 1]
    vmovdqu xmm0, xmmword ptr [\b + r10 - 15]
    shl r8d, 12
    imul edi, r9d, 128
    add edi, r8d
    sub r9, r10
    lea rdx, [rdx + r9*2]
    shl rdx, 6
    vpshufb xmm0, xmm0, xmmword ptr [shopping_click_campaigns + rdx + 4096]
    vpsubb xmm0, xmm0, xmmword ptr [shopping_click_campaigns + rdx + 4096 + 16]
    vpsubusb xmm2, xmm0, xmmword ptr [shopping_click_campaigns + rdx + 4096 + 32]
    vpmaddubsw xmm0, xmm0, xmm25
    vpmaddwd \camp, xmm0, xmm26
    vpshufb ymm3, ymm3, ymmword ptr [shopping_click_id_pairs + rdi]
    vpsubb ymm5, ymm3, ymmword ptr [shopping_click_id_pairs + rdi + 32]
    vpaddb ymm4, ymm5, ymm16
    vpsubusb ymm4, ymm4, ymm17
    vpsubusb ymm6, ymm5, ymm18
    vpminub ymm4, ymm4, ymm6
    vpaddb ymm6, ymm5, ymm19
    vpminub ymm5, ymm5, ymm6
    vmovdqa ymm6, ymmword ptr [shopping_click_id_pairs + rdi + 64]
    vpsubusb ymm6, ymm6, ymm5
    vpternlogq ymm4, ymm6, ymm2, 0xfe
    vpmaddubsw ymm5, ymm5, ymm20
    vpmaddwd ymm5, ymm5, ymm21
    vpmuludq ymm1, ymm5, ymm22
    vpsrlq ymm5, ymm5, 32
    vpaddq \ids, ymm1, ymm5
    lea rdx, [\b + r10]
    sub r10, \nlin
    imul \row, r10, -256
    add rdx, qword ptr [shopping_click_clocks + \row + 8192 + 176]
    movzx r9d, word ptr [rdx + 9]
    pext r9, r9, qword ptr [shopping_click_clocks + \row + 8192 + 184]
    shl r9d, 5
    vmovdqu64 \badx, xmmword ptr [r15 + r9 + 1536]
    vpxorq \badx, \badx, xmmword ptr [rdx]
    vpternlogq \bad, ymm4, ymmword ptr [shopping_click_clocks + \row + 8192 + 112], 0xec
    vmovdqu64 \days, xmmword ptr [r15 + r9 + 1536 + 16]
    vmovdqu64 \x, xmmword ptr [\a - 17]
.endm

.macro click_pair_finish o, lim, limx, row, x, days, bad, camp, idsx, extract, source
    vpshufb xmm0, \x, xmmword ptr [shopping_click_clocks + \row + 8192]
    vpsubb xmm0, xmm0, xmmword ptr [shopping_click_clocks + \row + 8192 + 16]
    vpmaddubsw xmm1, xmm0, xmmword ptr [shopping_click_clocks + \row + 8192 + 48]
    vpmaddwd xmm1, xmm1, xmmword ptr [shopping_click_clocks + \row + 8192 + 64]
    vpackusdw xmm2, xmm1, xmm1
    vpmaddwd xmm2, xmm2, xmmword ptr [shopping_click_clocks + \row + 8192 + 80]
    vpsubusb \limx, xmm0, xmmword ptr [shopping_click_clocks + \row + 8192 + 32]
    vpcmpgtd xmm1, xmm1, xmmword ptr [shopping_click_clocks + \row + 8192 + 96]
    vptestnmq k1, xmm0, xmm24
    vpmuludq xmm4, xmm2, xmmword ptr [shopping_click_clocks + \row + 8192 + 128]
    vpsrlq xmm2, xmm2, 32
    vpmuldq xmm2, xmm2, xmmword ptr [shopping_click_clocks + \row + 8192 + 144]
    vpsubq xmm2{k1}, xmm23, xmm2
    vpaddq xmm4, xmm4, \days
    vpaddq xmm4, xmm4, xmm2
    \extract xmm5, \source, 1
    vmovhps qword ptr [r15 + rbx + \o], xmm4
    vmovq qword ptr [r15 + rbx + \o + 512], \camp
    vmovq qword ptr [r15 + rbx + \o + 1024], \idsx
    vmovq qword ptr [r15 + rbx + \o + 1536], xmm5
    vpternlogq \lim, ymm1, \bad, 0xfe
.endm

.globl shopping_kernel_click_loop
.hidden shopping_kernel_click_loop
.type shopping_kernel_click_loop,@function
.p2align 5
shopping_kernel_click_loop:
    push r12
    push r13
    push r14
    push r15
    push rcx
    push rbx
    push rbp
    sub rsp, 24
    mov r15, rdx
    mov rbx, rsi
    sub rbx, r15
    vmovdqa ymm13, ymmword ptr [shopping_click_constants]
    vmovdqa ymm12, ymmword ptr [shopping_click_constants + 32]
    vmovdqa64 ymm16, ymmword ptr [shopping_click_constants + 64]
    vmovdqa64 ymm17, ymmword ptr [shopping_click_constants + 96]
    vmovdqa64 ymm18, ymmword ptr [shopping_click_constants + 128]
    vmovdqa64 ymm19, ymmword ptr [shopping_click_constants + 160]
    vmovdqa64 ymm20, ymmword ptr [shopping_click_constants + 192]
    vmovdqa64 ymm21, ymmword ptr [shopping_click_constants + 224]
    vmovdqa64 ymm22, ymmword ptr [shopping_click_constants + 256]
    vmovdqa64 xmm25, xmmword ptr [shopping_click_constants + 288]
    vmovdqa64 xmm26, xmmword ptr [shopping_click_constants + 304]
    vmovdqa64 xmm24, xmmword ptr [shopping_click_constants + 320]
    vpxord xmm23, xmm23, xmm23
    vpxor xmm14, xmm14, xmm14
    test bl, 8
    jnz 70f
    mov rbp, rdi
    mov qword ptr [rsp + 16], rbp
    mov qword ptr [rsp + 8], 1
    vpcmpeqb ymm0, ymm13, ymmword ptr [rbp]
    vpmovmskb eax, ymm0
    vpcmpeqb ymm1, ymm12, ymmword ptr [rbp + 32]
    vpmovmskb ecx, ymm1
    tzcnt ecx, ecx
    click_pair_prepare r12, rbp, eax, rcx, r11d, r13d, rsi, xmm8, xmm9, ymm10, xmm10, xmm11, ymm27
    jmp 2f
70:
    mov r12, rdi
    mov qword ptr [rsp + 16], r12
    sub rbx, 8
    mov qword ptr [rsp + 8], rbx
    vpcmpeqb ymm0, ymm13, ymmword ptr [r12]
    vpmovmskb r11d, ymm0
    vpcmpeqb ymm1, ymm12, ymmword ptr [r12 + 32]
    vpmovmskb r13d, ymm1
    tzcnt r13d, r13d
    click_pair_prepare rbp, r12, r11d, r13, eax, ecx, r14, xmm28, xmm29, ymm30, xmm30, xmm31, ymm7
    jmp 3f
    .p2align 5
2:
    click_pair_prepare rbp, r12, r11d, r13, eax, ecx, r14, xmm28, xmm29, ymm30, xmm30, xmm31, ymm7
    click_pair_finish 0, ymm14, xmm14, rsi, xmm8, xmm9, ymm10, xmm11, xmm27, vextracti32x4, ymm27
3:
    click_pair_prepare r12, rbp, eax, rcx, r11d, r13d, rsi, xmm8, xmm9, ymm10, xmm10, xmm11, ymm27
    click_pair_finish 8, ymm3, xmm3, r14, xmm28, xmm29, ymm30, xmm31, xmm7, vextracti128, ymm7
    vpor ymm3, ymm3, ymm14
    vptest ymm3, ymm3
    jnz 90f
    mov qword ptr [rsp + 16], rbp
    add rbx, 16
    jnz 2b
    mov rdi, rbp
    mov eax, 2
    lea rsi, [r15 + rbx]
    jmp 6f
90:
    mov rdi, qword ptr [rsp + 16]
    mov eax, 1
    vptest ymm14, ymm14
    jnz 91f
    lea rsi, [r15 + rbx + 8]
    cmp rbx, qword ptr [rsp + 8]
    je 6f
    vpcmpeqb ymm1, ymm12, ymmword ptr [rdi + 32]
    vpmovmskb ecx, ymm1
    tzcnt ecx, ecx
    lea rdi, [rdi + rcx + 33]
    jmp 6f
91:
    lea rsi, [r15 + rbx]
6:
    add rsp, 24
    pop rbp
    pop rbx
    pop rcx
    mov [rcx], rax
    mov [rcx+8], rdi
    mov [rcx+16], rsi
    pop r15
    pop r14
    pop r13
    pop r12
    vzeroupper
    ret
.size shopping_kernel_click_loop, .-shopping_kernel_click_loop
.section .note.GNU-stack,"",@progbits
