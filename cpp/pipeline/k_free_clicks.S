// Two staged rows share the decoder registers. The exit reports the first row
// needing repair, or the next row after filling the 64-slot output batch.
#if defined(__ELF__) && defined(__x86_64__)
.intel_syntax noprefix
.text
.equ KID1,937472
.equ UID1,941504
#ifdef SHOP_KERNEL_ABSOLUTE_TABLES
#define TABLE_ADDR shopping_kernel_tables
#define CAMP_ADDR shopping_kernel_camp
#define TIME_ADDR shopping_kernel_cts
#else
#define TABLE_ADDR 0
#define CAMP_ADDR 0
#define TIME_ADDR 0
#endif
.hidden shopping_kernel_tables
.hidden shopping_kernel_camp
.hidden shopping_kernel_cts

.macro cl_ms a, b, cin, nlin, cout, nlout, slot, row, x, days, bad, camp, ids
    lea \a, [\b + \nlin + 33]
    vpcmpeqb ymm0, ymm13, ymmword ptr [\a]
    vpmovmskb \cout, ymm0
    vpcmpeqb ymm1, ymm12, ymmword ptr [\a + 32]
    vpmovmskb \nlout, ymm1
    tzcnt \nlout, \nlout
    prefetcht0 byte ptr [\a + 1024]
    mov qword ptr [rsp + \slot], \b
    tzcnt r8d, \cin
    blsr \cin, \cin
    tzcnt r9d, \cin
    blsr \cin, \cin
    tzcnt r10d, \cin
    vmovdqu xmm3, xmmword ptr [\b + r8 - 16]
    vmovdqu xmm4, xmmword ptr [\b + r9 - 16]
    movzx edx, byte ptr [\b + r9 + 1]
    vmovdqu xmm0, xmmword ptr [\b + r10 - 15]
    imul edi, r9d, 64
    sub r9, r10
    and edx, 1
    lea rdx, [rdx + r9*2]
    shl rdx, 6
#ifndef SHOP_KERNEL_ABSOLUTE_TABLES
    lea r9, [rip + shopping_kernel_camp]
    add rdx, r9
#endif
    vpshufb xmm0, xmm0, xmmword ptr [CAMP_ADDR + rdx + 4096]
    vpsubb xmm0, xmm0, xmmword ptr [CAMP_ADDR + rdx + 4096 + 16]
    vmovdqa xmm1, xmmword ptr [CAMP_ADDR + rdx + 4096 + 32]
    vpsubusb xmm1, xmm1, xmm0
    vpsubusb xmm2, xmm0, xmm24
    vpmaddubsw xmm0, xmm0, xmm25
    vpmaddwd \camp, xmm0, xmm26
    shl r8d, 6
    sub edi, r8d
#ifndef SHOP_KERNEL_ABSOLUTE_TABLES
    lea r9, [rip + shopping_kernel_tables]
    add r8, r9
    add rdi, r9
#endif
    vpshufb xmm3, xmm3, xmmword ptr [TABLE_ADDR + r8 + KID1]
    vpsubb xmm3, xmm3, xmmword ptr [TABLE_ADDR + r8 + KID1 + 16]
    vpshufb xmm4, xmm4, xmmword ptr [TABLE_ADDR + rdi + UID1]
    vpsubb xmm4, xmm4, xmmword ptr [TABLE_ADDR + rdi + UID1 + 16]
    vpblendd xmm5, xmm3, xmm4, 12
    vpblendd xmm3, xmm4, xmm3, 12
    vpternlogq xmm3, xmm1, xmm2, 0xfe
    vpaddb xmm4, xmm5, xmm16
    vpsubusb xmm4, xmm4, xmm17
    vpsubusb xmm6, xmm5, xmm18
    vpminub xmm4, xmm6, xmm4
    vpaddb xmm6, xmm5, xmm19
    vpminub xmm5, xmm5, xmm6
    vmovdqu xmm6, xmmword ptr [TABLE_ADDR + rdi + UID1 + 32]
    vpor xmm6, xmm6, xmmword ptr [TABLE_ADDR + r8 + KID1 + 32]
    vpsubusb xmm6, xmm6, xmm5
    vpternlogq xmm3, xmm4, xmm6, 0xfe
    vpmaddubsw xmm5, xmm5, xmm20
    vpmaddwd xmm5, xmm5, xmm21
    vpmuludq xmm1, xmm5, xmm22
    vpsrlq xmm5, xmm5, 32
    vpaddq xmm5, xmm1, xmm5
    vporq \ids, xmm5, xmmword ptr [TABLE_ADDR + rdi + UID1 + 48]
    lea rdx, [\b + r10]
    sub r10, \nlin
    imul \row, r10, -256
#ifndef SHOP_KERNEL_ABSOLUTE_TABLES
    lea r9, [rip + shopping_kernel_cts]
    add \row, r9
#endif
    add rdx, qword ptr [TIME_ADDR + \row + 8192 + 176]
    movzx r9d, word ptr [rdx + 9]
    pext r9, r9, qword ptr [TIME_ADDR + \row + 8192 + 184]
    shl r9d, 5
    vmovdqu64 \bad, xmmword ptr [r15 + r9 + 1536]
    vpxorq \bad, \bad, xmmword ptr [rdx]
    vpternlogq \bad, xmm3, xmmword ptr [TIME_ADDR + \row + 8192 + 112], 0xec
    vmovdqu64 \days, xmmword ptr [r15 + r9 + 1536 + 16]
    vmovdqu64 \x, xmmword ptr [\a - 17]
.endm

.macro cl_d badx, full, row, x, days, bad, camp, ids
    vpshufb xmm0, \x, xmmword ptr [TIME_ADDR + \row + 8192]
    vpsubb xmm0, xmm0, xmmword ptr [TIME_ADDR + \row + 8192 + 16]
    vpmaddubsw xmm1, xmm0, xmmword ptr [TIME_ADDR + \row + 8192 + 48]
    vpmaddwd xmm1, xmm1, xmmword ptr [TIME_ADDR + \row + 8192 + 64]
    vpackusdw xmm2, xmm1, xmm1
    vpmaddwd xmm2, xmm2, xmmword ptr [TIME_ADDR + \row + 8192 + 80]
    vpsubusb xmm3, xmm0, xmmword ptr [TIME_ADDR + \row + 8192 + 32]
    vpcmpgtd xmm1, xmm1, xmmword ptr [TIME_ADDR + \row + 8192 + 96]
    vptestnmq k1, xmm0, xmmword ptr [TIME_ADDR + \row + 8192 + 160]
    vpmuludq xmm4, xmm2, xmmword ptr [TIME_ADDR + \row + 8192 + 128]
    vpsrlq xmm2, xmm2, 32
    vpmuldq xmm2, xmm2, xmmword ptr [TIME_ADDR + \row + 8192 + 144]
    vpsubq xmm2{k1}, xmm23, xmm2
    vpaddq xmm4, xmm4, \days
    vpaddq xmm4, xmm4, xmm2
    vmovhps qword ptr [r15 + rbx], xmm4
    vmovq qword ptr [r15 + rbx + 512], \camp
    vmovq qword ptr [r15 + rbx + 1024], \ids
    vmovhps qword ptr [r15 + rbx + 1536], \ids
    vpternlogq xmm3, xmm1, \bad, 0xfe
    vptest xmm3, xmm3
    jnz \badx
    add rbx, 8
    jz \full
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
    sub rsp, 16
    mov r15, rdx
    mov rbx, rsi
    sub rbx, r15
    mov rbp, rdi
    mov eax, 0x2c2c2c2c
    vmovd xmm13, eax
    vpbroadcastd ymm13, xmm13
    mov eax, 0x0a0a0a0a
    vmovd xmm12, eax
    vpbroadcastd ymm12, xmm12
    mov eax, 0xcfcfcfcf
    vpbroadcastd xmm16, eax
    mov eax, 0x19191919
    vpbroadcastd xmm17, eax
    mov eax, 0x09090909
    vpbroadcastd xmm18, eax
    vmovd xmm0, eax
    vmovdqa64 xmm24, xmm0
    mov eax, 0xd9d9d9d9
    vpbroadcastd xmm19, eax
    mov eax, 0x01240124
    vpbroadcastd xmm20, eax
    mov eax, 0x00010510
    vpbroadcastd xmm21, eax
    mov eax, 1679616
    vpbroadcastq xmm22, rax
    vpxord xmm23, xmm23, xmm23
    mov eax, 0x010a010a
    vmovd xmm0, eax
    vmovdqa64 xmm25, xmm0
    mov eax, 0x00010064
    vmovd xmm0, eax
    vmovdqa64 xmm26, xmm0
    vpcmpeqb ymm0, ymm13, ymmword ptr [rbp]
    vpmovmskb eax, ymm0
    vpcmpeqb ymm1, ymm12, ymmword ptr [rbp + 32]
    vpmovmskb ecx, ymm1
    tzcnt ecx, ecx
    cl_ms r12, rbp, eax, rcx, r11d, r13d, 0, rsi, xmm8, xmm9, xmm10, xmm11, xmm27
    .p2align 5
2:
    cl_ms rbp, r12, r11d, r13, eax, ecx, 8, r14, xmm28, xmm29, xmm30, xmm31, xmm7
    cl_d 92f, 82f, rsi, xmm8, xmm9, xmm10, xmm11, xmm27
    cl_ms r12, rbp, eax, rcx, r11d, r13d, 0, rsi, xmm8, xmm9, xmm10, xmm11, xmm27
    cl_d 93f, 83f, r14, xmm28, xmm29, xmm30, xmm31, xmm7
    jmp 2b
92:
    mov rdi, qword ptr [rsp]
    mov eax, 1
    jmp 6f
93:
    mov rdi, qword ptr [rsp + 8]
    mov eax, 1
    jmp 6f
82:
    mov rdi, qword ptr [rsp + 8]
    mov eax, 2
    jmp 6f
83:
    mov rdi, qword ptr [rsp]
    mov eax, 2
6:
    lea rsi, [r15 + rbx]
    add rsp, 16
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
#endif
