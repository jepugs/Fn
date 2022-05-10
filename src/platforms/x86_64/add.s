.global asm_add_of

.text

// perform addition and set *carry on overflow
// u64 add_of(u64* overflow, u64 a, u64 b)
asm_add_of:
    // move second argument
    mov %rdx, %rax
    add %rsi, %rax
    // add carry bit to *overflow
    adc $0, (%rdi)
    ret

