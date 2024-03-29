.type SupportsAvx, @function
.global SupportsAvx
SupportsAvx:
    push    %rbp
    mov     %rsp, %rbp
    push %rcx
    push %rdx
    mov $1, %rax
    cpuid
    andq $0x18000000, %rcx
    cmpq $0x18000000, %rcx    # check both OSXSAVE and AVX feature flags
    jne NotSupported         
                       # processor supports AVX instructions and XGETBV is enabled by OS
    mov $0, %rcx       # specify 0 for XFEATURE_ENABLED_MASK register
                       # 0xd0010f is xgetbv  - result in EDX:EAX 
    .byte 0xf, 0x1, 0xd0
    andq $6, %rax
    cmpq $6, %rax      # check OS has enabled both XMM and YMM state support
    jne NotSupported
	andq $0x10, %rbx
    cmpq $0x10, %rbx    # no AVX2
    jne NotSupported
    mov $1, %rax
done:
    pop %rdx
    pop %rcx
    leave
    ret


NotSupported:
    mov $0, %rax
    jmp done
