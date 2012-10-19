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
#include <iostream>
#include <fstream>
#include "pin.H"
#include "instlib.H"
#include "portability.H"

using namespace std;

ofstream OutFile;

// The running count of instructions is kept here
// make it static to help the compiler optimize docount
enum{
  cDYN,
  cINT,
  cFL,
  cLD,
  cST,
  cBR,
  cBRT,
  cFBRT,
  INS_COUNT_SIZE
};

static UINT64 ins_count[INS_COUNT_SIZE];
/*
static UINT64 dyn_count = 0;
static UINT64 int_count = 0;
static UINT64 fl_count  = 0;
static UINT64 ld_count  = 0;
static UINT64 st_count  = 0;
static UINT64 br_count  = 0;
//static UINT64 brt_count = 0;
//static UINT64 fbrt_count = 0;
*/

// This function is called before every instruction is executed
VOID docount(UINT32 type) { 
  ins_count[cDYN]++;
  ins_count[type]++;
}
    
// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    // Insert a call to docount before every instruction, no arguments are passed
    //Parse instruction type
    UINT32 type;
    //cout << "INST: " << CATEGORY_StringShort(INS_Category(ins)) << endl;
    if(INS_IsMemoryRead(ins)) type=cLD;
    if(INS_IsMemoryWrite(ins)) type=cST;
    if(INS_IsBranchOrCall(ins)) type=cBR;
    else type=cDYN;
    type=cDYN;
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_UINT32, type, IARG_END);
}

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "inscount.out", "specify output file name");

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    // Write to a file since cout and cerr maybe closed by the application
    OutFile.setf(ios::showbase);
    OutFile << "Count " << ins_count[cDYN] << endl;
    OutFile.close();
    cout << "Dynamic:\t" << ins_count[cDYN] << endl;
    cout << "Integer:\t" << ins_count[cINT] << endl;
    cout << "Float:\t" << ins_count[cFL] << endl;
    cout << "Load:\t" << ins_count[cLD] << endl;
    cout << "Store:\t" << ins_count[cST] << endl;
    cout << "Branch:\t" << ins_count[cBR] << endl;
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This tool counts the number of dynamic instructions executed" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */
/*   argc, argv are the entire command line: pin -t <toolname> -- ...    */
/* ===================================================================== */

int main(int argc, char * argv[])
{
    // Clear Counts
    for(int i=0; i<INS_COUNT_SIZE; i++) ins_count[i]=0;
    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

    OutFile.open(KnobOutputFile.Value().c_str());

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}
