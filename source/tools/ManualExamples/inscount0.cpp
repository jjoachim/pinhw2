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
#include <vector>
#include <math.h>
#include "pin.H"
#include "portability.H"

using namespace std;

ofstream OutFile;

// Various instruction counts.  BBLs tracked separately
enum{
  cDYN,
  cINT,
  cFL,
  cLD,
  cST,
  cBR,
  cBRT,
  cFBRT,
  cMISC,
  INS_COUNT_SIZE
};

// Counting structures
static UINT64 ins_count[INS_COUNT_SIZE]; //The running count of instructions is kept here
static int lbr_dist=0; //Last branch distance
static vector<int> bblsz; //Basic Block Size list
static vector<int> regs; //A list of values that indicate a register's last use.  Used for RAW, WAW, and WAR

// This function updates instruction counts
VOID InsCountF(UINT32 type) {
  lbr_dist++; //Every instruction increases the distance between two branches
  ins_count[cDYN]++;
  ins_count[type]++;
}

// This function updates instruction flow from branches and calls
VOID BrCallCountF(ADDRINT current, ADDRINT target, BOOL taken){
  //Branch Distance calculation
  bblsz.push_back(lbr_dist); //Remember the distance
  lbr_dist=0; //Reset the distance, note that it is incremented in InsCountF() which is called in branches too
  //He's going the distance! He's going for speed! http://www.youtube.com/watch?v=__PU5CVSegg
  //Branch Taken counters
  if(taken){
    ins_count[cBRT]++;
    if(target>current) ins_count[cFBRT]++;
  }
}

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    //Parse instruction type
    UINT32 type=cMISC;
    if(INS_IsMemoryRead(ins)) type=cLD;
    else if(INS_IsMemoryWrite(ins)) type=cST;
    else if(INS_IsBranchOrCall(ins)){ //handle branch parsing
      type=cBR;
      INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)BrCallCountF, IARG_INST_PTR, IARG_BRANCH_TARGET_ADDR, IARG_BRANCH_TAKEN, IARG_END);
    }
    else{
      if(INS_OperandCount(ins)){
        if(INS_OperandIsReg(ins,0)){
          REG reg=INS_OperandReg(ins,0);
          if(REG_is_fr(reg)) type=cFL;
          else type=cINT;
        }
      }
    }
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)InsCountF, IARG_UINT32, type, IARG_END);
}

// Commandline Switch Parsing
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "inscount.out", "specify output file name");

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    //Basic Block Size: Mean
    long int bblsz_mean=0;
    for (unsigned int i=0; i<bblsz.size(); i++)
      bblsz_mean+=bblsz[i]; 
    bblsz_mean/=(bblsz.size());
    //Basic Block Size: Standard Deviation
    double bblsz_stdv=0;
    for (unsigned int i=0; i<bblsz.size(); i++){
      int diff=bblsz[i]-bblsz_mean;
      bblsz_stdv+=(diff*diff);
    }
    bblsz_stdv=sqrt( bblsz_stdv/(double)bblsz.size() );
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
    cout << "BranchT:\t" << ins_count[cBRT] << endl;
    cout << "FBranchT:\t" << ins_count[cFBRT] << endl;
    cout << "BBranchT:\t" << ins_count[cBRT]-ins_count[cFBRT] << endl;
    cout << "Other:\t" << ins_count[cMISC] << endl;
    cout << "BBL Count:\t" << bblsz.size() << endl;
    cout << "BBL Size Avg:\t" << bblsz_mean << endl;
    cout << "BBL Size STDV:\t" << bblsz_stdv << endl;
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
