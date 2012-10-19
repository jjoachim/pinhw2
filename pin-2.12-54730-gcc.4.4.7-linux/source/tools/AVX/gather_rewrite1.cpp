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
#include <stdio.h>
#include "pin.H"

// simple vgather* emulator, using the IARG_MEMORYOP_EA and IARG_MEMORYOP_MASKED_ON.

UINT32 totalPossibleWriteSize=0;
VOID EmuGatherMemOp (UINT32 memIndex, ADDRINT memoryAddress, BOOL maskedOn, UINT32 memAccessSize, UINT32 isRead, PIN_REGISTER* ymmDest, bool destIsYmm)
{
    totalPossibleWriteSize += memAccessSize;
    if (maskedOn)
    {
        if (memAccessSize == 4)
        {
            ymmDest->dword[memIndex] = *(reinterpret_cast<UINT32 *>(memoryAddress));
        }
        else
        {
            ASSERTX(memAccessSize == 8);
            ymmDest->qword[memIndex] = *(reinterpret_cast<UINT64 *>(memoryAddress));
        }
    }
}

VOID EmuGatherFinal (PIN_REGISTER* ymmDest, PIN_REGISTER* ymmMask)
{
    if (totalPossibleWriteSize<32)
    {
        ymmDest->qword[3] = 0;
        ymmDest->qword[2] = 0;
        if (totalPossibleWriteSize<16)
        {
            ymmDest->qword[1] = 0;
        }
    }
    ymmMask->qword[0] = 0;
    ymmMask->qword[1] = 0;
    ymmMask->qword[2] = 0;
    ymmMask->qword[3] = 0;
    totalPossibleWriteSize = 0;
}

static REG GetScratchReg(UINT32 index)
{
    static std::vector<REG> regs;

    while (index >= regs.size())
    {
        REG reg = PIN_ClaimToolRegister();
        if (reg == REG_INVALID())
        {
            std::cerr << "*** Ran out of tool registers" << std::endl;
            PIN_ExitProcess(1);
            /* does not return */
        }
        regs.push_back(reg);
    }

    return regs[index];
}

static ADDRINT ManipulateMemAddress(ADDRINT ea)
{
    return ea;
}

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    if (INS_IsVgather(ins))
    {
        REG ymmDest = INS_OperandReg(ins, 0), ymmMask = INS_OperandReg(ins, 2);
        if (!REG_is_ymm(ymmDest))
        {
            ymmDest = (REG)(ymmDest - REG_XMM0 + REG_YMM0);
        }
        if (!REG_is_ymm(ymmMask))
        {
            ymmMask = (REG)(ymmMask - REG_XMM0 + REG_YMM0);
        }
                
        for (UINT32 memIndex = 0;  
            memIndex < INS_MemoryOperandCount(ins);//each access is 1 MemoryOperand  	                                    
            memIndex++)
        {
                INS_InsertCall(ins, IPOINT_BEFORE, AFUNPTR(EmuGatherMemOp),
                            IARG_UINT32, memIndex,
                            IARG_MEMORYOP_EA, memIndex,
                            IARG_MEMORYOP_MASKED_ON, memIndex,
                            IARG_UINT32, INS_MemoryOperandSize(ins, memIndex),
                            IARG_UINT32, INS_MemoryOperandIsRead(ins, memIndex),
                            IARG_REG_REFERENCE, ymmDest,
                            IARG_BOOL, REG_is_ymm(INS_OperandReg(ins, 0)),
                            IARG_END);
                REG scratchReg = GetScratchReg(memIndex);
                INS_InsertCall(ins, IPOINT_BEFORE, AFUNPTR(ManipulateMemAddress),
                               IARG_MEMORYOP_EA, memIndex,
                               IARG_RETURN_REGS, scratchReg,
                               IARG_END);
                INS_RewriteMemoryOperand(ins, memIndex, scratchReg); 
        }

        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)EmuGatherFinal,
                        IARG_REG_REFERENCE, ymmDest,
                        IARG_REG_REFERENCE, ymmMask,
                        IARG_END);
        INS_Delete(ins);
    }
}


/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */
/*   argc, argv are the entire command line: pin -t <toolname> -- ...    */
/* ===================================================================== */

int main(int argc, char * argv[])
{
    // Initialize pin
    PIN_Init(argc, argv);

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}
