/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2012 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
#include "gsseemu.h"
static int res = 0;

static M256 old_dst, vind, mask, d, YMMDestBefore, YMMIndexBefore, YMMIndexAfter, YMMMaskBefore, YMMMaskAfter, expect;
static int scale;

static int arr[] = {0x12345678, 0x23456789, 0x076534af, 0x654234bc,
             0xf0367678, 0x82643618, 0x1e34b678, 0x1abcf6e8,
             0x19476678, 0x83356729, 0xf7f5f4ff, 0xe5e2e4ec,
             0xfb3c7a73, 0x8a6abc18, 0x1a3aba7a, 0x1aaca6e8};

char *ptr = (char *)arr;

#if defined(_M_X64) || defined(__x86_64__)
#define REG rax
#define ADDRPTR QWORD PTR
#else
#define REG eax
#define ADDRPTR DWORD PTR
#endif

void check_res(unsigned n)
{
    int i;
    for(i = 0; i < 4; i++){
        if(d.l[i] != expect.l[i]){
            printf("Case %d FAILED\n", n);
            printf("result[%d] %g, expected %g\n", i, d.d[i], expect.d[i]);
            printVd("old_dst:", 4, old_dst.d);
            printVi("vind:   ", vind.i);
            printVl("mask:   ", mask.l);
            printVd("expect:", 4, expect.d);
            printVd("d:     ", 4, d.d);
            printVl("expect_l:", expect.l);
            printVl("d_l:     ", d.l);
            printVl("mask_b_l:", YMMMaskBefore.l);
            printVl("mask_a_l:", YMMMaskAfter.l);
            res = 1;
            return;
        }
    }
    for (i=0; i < 8; i++) {
        if (YMMMaskAfter.i[i] != 0)  {
                printf("Failed: mask byte# %d expected 0 observed %x\n", i, YMMMaskAfter.i[i]);
                res = 1;
                return;
        }
    }
    PRINTF("Case %d passed\n", n);
}

int main()
{
    int i;

    init_double(old_dst.d, 4, 3.14);	/* Initialize memory and the registers */

    vind.i[0] = 0;
    vind.i[1] = 6;
    vind.i[2] = 3;
    vind.i[3] = 1;
    vind.i[4] = 7;
    vind.i[5] = 11;
    vind.i[6] = 13;
    vind.i[7] = 10;

    mask.l[0] = 0x80ffffff00000000LL;
    mask.l[1] = 0x00ffffff00ffffffLL;
    mask.l[2] = 0x0000000000ffffffLL;
    mask.l[3] = 0x80ffffff80ffffffLL;
    
    init_long(&d, 0xdeadbeef);
    for (i = 0; i < 4; i++) {
        expect.d[i] = (mask.l[i] & 0x8000000000000000LL)
            ? *(double *)(ptr+(vind.i[i] * 8 + 0))
            : old_dst.d[i];
    }

    __asm {     /* VGATHERDPD ymm1, [rax + ymm2_vind*8], ymm3_mask */
        lea REG, ADDRPTR [arr-4] /* the memory rewrite of the vgather will add 4 back to the address */
        vmovupd YMMa, [old_dst.md];
        vmovupd YMMb, [vind.md];
        vmovupd YMMc, [mask.md];
        vmovupd [YMMDestBefore.ms],  YMMa;
        vmovupd [YMMIndexBefore.ms], YMMb;
        vmovupd [YMMMaskBefore.ms],  YMMc;
        vgatherdpd YMMa, [REG + XMMb*8], YMMc
        vmovupd [d.md], YMMa;
        vmovupd [YMMIndexAfter.ms], YMMb;
        vmovupd [YMMMaskAfter.ms],  YMMc;
    }
    printVl("YMM dest  before:     ", YMMDestBefore.l);
    printVl("YMM dest  after:      ", d.l);
    printVl("YMM index before:     ", YMMIndexBefore.l);
    printVl("YMM index after:      ", YMMIndexAfter.l);
    printVl("YMM mask  before:     ", YMMMaskBefore.l);
    printVl("YMM mask  after:      ", YMMMaskAfter.l);
    check_res(1);

    init_long(&d, 0xdeadbeef);
    for (i = 2; i < 4; i++) {
        expect.d[i] = 0;
    }
    __asm {     /* VGATHERDPD xmm1, [rax + xmm2_vind*8], xmm3_mask */
        lea REG, ADDRPTR [arr-4] /* the memory rewrite of the vgather will add 4 back to the address */
        vmovupd YMMa, [old_dst.md];
        vmovupd YMMb, [vind.md];
        vmovupd YMMc, [mask.md];
        vmovupd [YMMDestBefore.ms],  YMMa;
        vmovupd [YMMIndexBefore.ms], YMMb;
        vmovupd [YMMMaskBefore.ms],  YMMc;
        vgatherdpd XMMa, [REG + XMMb*8], XMMc
        vmovupd [d.md], YMMa;
        vmovupd [YMMIndexAfter.ms], YMMb;
        vmovupd [YMMMaskAfter.ms],  YMMc;
    }
    printVl("YMM dest  before:     ", YMMDestBefore.l);
    printVl("YMM dest  after:      ", d.l);
    printVl("YMM index before:     ", YMMIndexBefore.l);
    printVl("YMM index after:      ", YMMIndexAfter.l);
    printVl("YMM mask  before:     ", YMMMaskBefore.l);
    printVl("YMM mask  after:      ", YMMMaskAfter.l);
    check_res(2);

    init_long(&d, 0xdeadbeef);
    for (i = 0; i < 4; i++) {
        expect.d[i] = (mask.l[i] & 0x8000000000000000LL)
            ? *(double *)(ptr+(vind.i[i] * 8 + 8))
            : old_dst.d[i];
    }
    __asm {     /* VGATHERDPD ymm1, [rax + ymm2_vind*8 + 8], ymm3_mask */
        lea REG, ADDRPTR [arr-4] /* the memory rewrite of the vgather will add 4 back to the address */
        vmovupd YMMa, [old_dst.md];
        vmovupd YMMb, [vind.md];
        vmovupd YMMc, [mask.md];
        vmovupd [YMMDestBefore.ms],  YMMa;
        vmovupd [YMMIndexBefore.ms], YMMb;
        vmovupd [YMMMaskBefore.ms],  YMMc;
        vgatherdpd YMMa, [REG + XMMb*8 + 8], YMMc
        vmovupd [d.md], YMMa;
        vmovupd [YMMIndexAfter.ms], YMMb;
        vmovupd [YMMMaskAfter.ms],  YMMc;
    }
    printVl("YMM dest  before:     ", YMMDestBefore.l);
    printVl("YMM dest  after:      ", d.l);
    printVl("YMM index before:     ", YMMIndexBefore.l);
    printVl("YMM index after:      ", YMMIndexAfter.l);
    printVl("YMM mask  before:     ", YMMMaskBefore.l);
    printVl("YMM mask  after:      ", YMMMaskAfter.l);
    check_res(3);

    init_long(&d, 0xdeadbeef);
    for (i = 2; i < 4; i++) {
        expect.d[i] = 0;
    }
    __asm {     /* VGATHERDPD xmm1, [rax + xmm2_vind*8 + 8], xmm3_mask */
        lea REG, ADDRPTR [arr-4] /* the memory rewrite of the vgather will add 4 back to the address */
        vmovupd YMMa, [old_dst.md];
        vmovupd YMMb, [vind.md];
        vmovupd YMMc, [mask.md];
        vmovupd [YMMDestBefore.ms],  YMMa;
        vmovupd [YMMIndexBefore.ms], YMMb;
        vmovupd [YMMMaskBefore.ms],  YMMc;
        vgatherdpd XMMa, [REG + XMMb*8 + 8], XMMc
        vmovupd [d.md], YMMa;
        vmovupd [YMMIndexAfter.ms], YMMb;
        vmovupd [YMMMaskAfter.ms],  YMMc;
    }
    printVl("YMM dest  before:     ", YMMDestBefore.l);
    printVl("YMM dest  after:      ", d.l);
    printVl("YMM index before:     ", YMMIndexBefore.l);
    printVl("YMM index after:      ", YMMIndexAfter.l);
    printVl("YMM mask  before:     ", YMMMaskBefore.l);
    printVl("YMM mask  after:      ", YMMMaskAfter.l);
    check_res(4);

    if(!res) PRINTF("gatherdpd passed\n");
    return res;
}
