.global asm_mul_of

.text

// perform a 64x64->128 bit multiplication
// u64 mul_of(u64* hi, u64 a, u64 b)
asm_mul_of:
    // move 2nd argument for multiplication
    mov %rdx, %rax
    // multiply by 3rd argument
    mul %rsi
    // set *hi to the upper machine word
    mov %rdx, (%rdi)
    ret
