.text
.globl wx_opus_detour
.def wx_opus_detour; .scl 2; .type 32; .endef
.seh_proc wx_opus_detour
wx_opus_detour:
    subq $200, %rsp
    .seh_stackalloc 200
    .seh_endprologue
    movq %rcx, 32(%rsp)
    movq %rdx, 40(%rsp)
    movq %r8, 48(%rsp)
    movq %r9, 56(%rsp)
    movq %rax, 64(%rsp)
    movq %r10, 72(%rsp)
    movq %r11, 80(%rsp)
    pushfq
    popq %rax
    movq %rax, 88(%rsp)
    movdqu %xmm0, 96(%rsp)
    movdqu %xmm1, 112(%rsp)
    movdqu %xmm2, 128(%rsp)
    movdqu %xmm3, 144(%rsp)
    movdqu %xmm4, 160(%rsp)
    movdqu %xmm5, 176(%rsp)
    call wx_adjust_opus
    movq %rax, 48(%rsp)
    movdqu 96(%rsp), %xmm0
    movdqu 112(%rsp), %xmm1
    movdqu 128(%rsp), %xmm2
    movdqu 144(%rsp), %xmm3
    movdqu 160(%rsp), %xmm4
    movdqu 176(%rsp), %xmm5
    movq 88(%rsp), %rax
    pushq %rax
    popfq
    movq 32(%rsp), %rcx
    movq 40(%rsp), %rdx
    movq 48(%rsp), %r8
    movq 56(%rsp), %r9
    movq 64(%rsp), %rax
    movq 72(%rsp), %r10
    movq 80(%rsp), %r11
    addq $200, %rsp
    jmp *wx_opus_original(%rip)
.seh_endproc
