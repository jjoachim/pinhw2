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
#include <list>
#include <math.h> //for sqrt()
#include "pin.H"
#include "portability.H"

#define RDD_BINS 6 //This means track dependency distance from 2 to 2^n in increments of powers of 2.

using namespace std;

/* NOTES:
 * RDD means Register Dependency Distance
 */


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
enum{
  cRAW,
  cWAW,
  cWAR,
  RDD_SIZE,
};

// Struct for RDD tracking
struct Register_Use{ //Used when instruction used a register
  int ID; //REG ID or count, based on context
  int Rd_Wr; //0 is Read, 1 is Write
};
struct Register_Hist{ //Used to track register use history for RDD
  int ID;
  int Wr_Clk; //Clock when last write occurred
  int Rd_Clk; //Clock when last read occurred
};
struct MemRefTrack{ //Used to track memory references
  ADDRINT addr; //address of memory reference
  int repeat; //0 if this address is only referenced once, aka infinite inter-reference
};

struct MemRefCount{ //Used Track memory reference distances
  MemRefCount(int dist, int c) {distance=dist; count=c;}
  MemRefCount() { MemRefCount(0,0); }
  bool operator== (MemRefCount mrc) { return distance == mrc.distance; } //Used for the find() function
  bool operator<  (MemRefCount mrc) { return distance <  mrc.distance; } //Used for the sort() function
  int distance; //how far was it
  int count; //how many times has this distance occurred
};

// Counting structures
static UINT64 ins_count[INS_COUNT_SIZE]; //The running count of instructions is kept here
static int lbr_dist=0; //Last branch distance
static vector<int> bblsz; //Basic Block Size list
static Register_Hist regs[REG_LAST]; //A list of values that indicate a register's last use.  Used for RAW, WAW, and WAR
static unsigned int RDD_count[RDD_SIZE][RDD_BINS]; //Stores RAW, WAW, and WAR counts
static list<MemRefTrack> mf_track; //list of memory references to measure temporal locality
static list<MemRefCount> mf_count; //list of memory reference distances
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

// These functions update RAW, WAW, and WAR stats.
// This function sorts a value in distance bins based on RDD_BINS (<=2, <=4, ect...)
int ghetto_log2(int val){
  //if(val<=0) cout << "Val: " << val << endl;
  if (val<=0) return 0; //Just to avoid any dumb problems
  int bin=-1;
  while(val>>=1) bin++; //this loops will execute at least once since val must be >0
  if(bin>=RDD_BINS) bin=RDD_BINS-1; //don't go over the maximum bin
  return bin;
}

VOID RDDCountF(VOID* arglist, UINT32 size){
  Register_Use* ru=(Register_Use*)arglist;  //Arglist points to an array of Operand_Used structs
  int ID; //Register ID
  int dist; //The distance between the last access
  for(UINT32 i=0; i<size; i++){
    ID=ru[i].ID;
    if(ID==0) continue; //The operand was not a register! Skip it. (Probably an immidiate)
    //Access type, aka RAW, WAW, or WAR.  RAR is skipped
    if(ru[i].Rd_Wr==0){ //Reads update just RAW
      dist=ins_count[cDYN]-regs[ID].Wr_Clk; //Current_Clock - LastWrite_Clock
      RDD_count[cRAW][ghetto_log2(dist)]++; //increment the corresponding bin
     
      regs[ID].Rd_Clk=ins_count[cDYN]; //update LastRead_Clock for this register
    }
    else{ //Writes update WAW and WAR
      //WAW update first (order does not particularly matter)
      dist=ins_count[cDYN]-regs[ID].Wr_Clk;
      RDD_count[cWAW][ghetto_log2(dist)]++;
      //WAR update
      dist=ins_count[cDYN]-regs[ID].Rd_Clk;
      RDD_count[cWAR][ghetto_log2(dist)]++;
      
      regs[ID].Wr_Clk=ins_count[cDYN]; //update LastWrite_Clock for this register
    }
  }
}

// These function tracks memory references for temporal locality measurement
VOID MemRefCountF(ADDRINT addr){
  list<MemRefTrack>::iterator it;
  int dist=0;
  for(it=mf_track.begin(); it!=mf_track.end(); it++){
    if((it->addr)==addr){ //found a matching address!
      //find if we have seen this distance before
      MemRefCount mfc(dist,0);
      list<MemRefCount>::iterator mfc_it=find(mf_count.begin(), mf_count.end(), mfc);
      if(mfc_it==mf_count.end()){ //didn't find it, add it to the list
        mfc.count=1;
        mf_count.push_back(mfc);
      }
      else //found it! update the count
        (mfc_it->count)++;
      //Update the LRU list for addresses
      MemRefTrack new_ref=*it; //remember it
      new_ref.repeat=1; //we know it's repeated
      mf_track.erase(it); //remove it
      mf_track.push_front(new_ref); //stick it back to the front
      return;
    }
    dist++;
  }
  //It's a new address reference.  Add it to the list at the front
  MemRefTrack new_ref;
  new_ref.addr=addr;
  new_ref.repeat=0;
  mf_track.push_front(new_ref);
}

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    //Parse instruction type
    UINT32 type=cMISC;
    if(INS_IsMemoryRead(ins)){
      type=cLD; //check memory read operation
      INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MemRefCountF, IARG_MEMORYREAD_EA, IARG_END);
    }
    else if(INS_IsMemoryWrite(ins)){
      type=cST; //check memory write operation
      INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MemRefCountF, IARG_MEMORYWRITE_EA, IARG_END);
    }
    else if(INS_IsBranchOrCall(ins)){ //check branch operation
      type=cBR;
      INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)BrCallCountF, IARG_INST_PTR, IARG_BRANCH_TARGET_ADDR, IARG_BRANCH_TAKEN, IARG_END);
    }
    else{ //check arithmetic operation
      UINT32 opcount=INS_OperandCount(ins); //Total operands in instruction
      if(opcount){ //If there's at least one, the first one must be a destination register
        if(INS_OperandIsReg(ins,0)){ //check to make sure that it is a destination for shits n giggles
          REG reg=INS_OperandReg(ins,0);
          if(REG_is_fr(reg)) type=cFL; //If the DR is a floating point, then it is a floating point operation
          else type=cINT; //otherwise, it must be an integer operation
        }
      }
      Register_Use* ops_used=new Register_Use[opcount]; //A list of operands used in this instruction
      for(UINT32 i=0; i<opcount; i++){ //Parse through all the operands, list their ID and if they are R_Wr
        ops_used[i].ID=(int)INS_OperandReg(ins,i); //immidiates are stored as ID=0
        ops_used[i].Rd_Wr=INS_OperandWritten(ins,i); //0 is Read, 1 is Write
      }
      //Tracks RDD stuff
      INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RDDCountF, IARG_PTR, (VOID*)ops_used, IARG_UINT32, opcount, IARG_END);
    }
    //Tracks instruction distribution
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)InsCountF, IARG_UINT32, type, IARG_END);
}

// Commandline Switch Parsing
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "inscount.out", "specify output file name");

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    // Basic Block Size: Mean
    long int bblsz_mean=0;
    for (unsigned int i=0; i<bblsz.size(); i++)
      bblsz_mean+=bblsz[i]; 
    bblsz_mean/=(bblsz.size());
    // Basic Block Size: Standard Deviation
    double bblsz_stdv=0;
    for (unsigned int i=0; i<bblsz.size(); i++){
      int diff=bblsz[i]-bblsz_mean;
      bblsz_stdv+=(diff*diff);
    }
    bblsz_stdv=sqrt( bblsz_stdv/(double)bblsz.size() );
    // Sort Memory Reference Distances, find single memory references
    mf_count.sort();

    MemRefCount mfc(-1,0);
    mf_count.push_back(mfc);
    for(list<MemRefTrack>::iterator it=mf_track.begin(); it!=mf_track.end(); it++)
      if((it->repeat)==0) mf_count.back().count++;
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
    cout << "\nRDD Stats:\n";
    for(int i=0; i<RDD_SIZE; i++){
      cout << "Type " << i << ':';
      for(int j=0; j<RDD_BINS; j++){
        cout << '\t' << RDD_count[i][j];
      }
      cout << endl;
    }
    cout << "\nInter-Reference Stats:\n";
    //for(list<MemRefCount>::iterator it=mf_count.begin(); it!=mf_count.end(); it++)
    list<MemRefCount>::iterator it=mf_count.end();
    cout << "Distance " << it->distance  << ": " << it->count << endl;
    cout << "Size: " << mf_count.size() << endl;
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
    // Initialize Instruction Counts
    for(int i=0; i<INS_COUNT_SIZE; i++) 
      ins_count[i]=0;
    // Initialize RDD Counts and Registers
    for(int i=0; i<REG_LAST; i++){ //Reset LastWrite_Clock and LastRead_Clock for registers
      regs[i].Wr_Clk=0;
      regs[i].Rd_Clk=0;
    }
    for(int i=0; i<RDD_SIZE; i++)
      for(int j=0; j<RDD_BINS; j++)
        RDD_count[i][j]=0; //Reset RAW, WAW, and WAR counts.

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
