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
#define TEMPLOC_BINS 10 //the number of temporal locality distances, excluding infinite
//Optional defines.  Turn turn off a feature, comment out the definition
//#define MEMREF_GARBAGE_COLLECT //deletes consumed memory references to save memory
//#define TRACK_TEMPORAL //undefine this to turn off all temporal calculations
#define TRACK_BBL_SZ  //undefine this to turn off all 

#ifdef TEMPLOC_BINS
  #define MF_COUNT_TYPE vector //if we know the number of bins, vector is better
  #define MF_SORT(X,Y) sort(X.begin(), X.end(), Y) //vector sort
#else
  #define MF_COUNT_TYPE list //list is better for arbitrary length
  #define MF_SORT(X,Y) x.sort() //list sort
#endif

using namespace std;

/* NOTES:
 * RDD means Register Dependency Distance
 */

//This array specifies how many LSB to ignore in address references when measuring temporal locality
//char TempLocGran[]={0, 5, 11}; //ignore 0, 5 (32B), and 11 (2KB) bits of the referenced addresses respectively
char TempLocGran[]={5,11}; 
PIN_THREAD_UID ThreadUID[sizeof(TempLocGran)]; //an exit code for each granularity
int SlaveMemParse[sizeof(TempLocGran)]; //how many memory locations have the slaves parsed (for moderator)
int ModMemParse=0; //How mnay memory references has the Moderator cleaned up

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
  int repeat; //0 if this address is only referenced once, aka infinite inter-reference.  Also used to track Rd/Wr status
};

struct Counter{ //Used Track memory reference distances and block sizes
  Counter(int dist, int c) {distance=dist; count=c;}
  Counter() { Counter(0,0); }
  bool operator== (const Counter& c) { return this->distance == c.distance; } //Used for the find() function
  //bool operator<  (const Counter& c) { return this->distance <  c->distance; } //Used for the sort() function
  int distance; //how far was it
  int count; //how many times has this distance occurred
};
bool Counter_LT (const Counter& c1, const Counter& c2){ return c1.distance <  c2.distance; } //Used for the sort()/bindary_find() functions

// Counting structures
static UINT64 ins_count[INS_COUNT_SIZE]; //The running count of instructions is kept here

static int lbr_dist=0; //Last branch distance
static int blk_count=0; //Number of blocks found
list<int> blksz_FIFO; //FIFO for block size tracking thread
vector<Counter> blksz_vect; //List of unique block size

static Register_Hist regs[REG_LAST]; //A list of values that indicate a register's last use.  Used for RAW, WAW, and WAR
static unsigned int RDD_count[RDD_SIZE][RDD_BINS]; //Stores RAW, WAW, and WAR counts

list<ADDRINT> memref_FIFO; //FIFO for slave threads to read
int FIFOsize=0;
MF_COUNT_TYPE<Counter> mfc[sizeof(TempLocGran)]; //different temporal locality charts
int thread_exit=0; //signal set by master to make the moderator and slaves exit

// This function updates instruction counts
VOID InsCountF(UINT32 type) {
  lbr_dist++; //Every instruction increases the distance between two branches
  ins_count[cDYN]++;
  ins_count[type]++;
}

// This function updates instruction flow from branches and calls
VOID BrCallCountF(ADDRINT current, ADDRINT target, BOOL taken){
  //Branch Distance calculation
  #ifdef TRACK_BBL_SZ
    blksz_FIFO.push_back(lbr_dist); //add new distance to fifo
    blk_count++; //increment the total count
    lbr_dist=0; //Reset the distance, note that lbr_dist is incremented in InsCountF() which is called in branches too
    //He's going the distance! He's going for speed! http://www.youtube.com/watch?v=__PU5CVSegg
  #endif
  //Branch Taken counters
  if(taken){
    ins_count[cBRT]++;
    if(target>current) ins_count[cFBRT]++;
  }
}

// Block Size Avg and STDV thread
VOID Para_BlockSizeTrackF(VOID* arg){
  int Blocks_Parsed=3;
  blksz_vect.reserve(1000); //give it a good size so it doesn't need to realloc
  vector<Counter>::iterator it; //used for binary_search results
  Counter new_blk(0,1); //used as comparison element
  while(thread_exit==0 || Blocks_Parsed<blk_count){
    for(; Blocks_Parsed<blk_count; Blocks_Parsed++){
      new_blk.distance=blksz_FIFO.front();
      it=lower_bound(blksz_vect.begin(), blksz_vect.end(), new_blk, Counter_LT);
      if((it->distance)==new_blk.distance) //it was found!
        (it->count)++; //increment the count
      else //it was not found
        blksz_vect.insert(it,new_blk); //insert it in the correct spot, inherently keeps it sorted

      blksz_FIFO.pop_front(); //get rid of old element
      //Note: Blocks_Parsed incremented in the forloop
    }
    thread_exit=PIN_IsProcessExiting();
  } 
}

// These functions update RAW, WAW, and WAR stats.
// This function sorts a value in distance bins based on RDD_BINS (<=2, <=4, ect...)
int ghetto_log2(int val, int max){
  //if(val<=0) cout << "Val: " << val << endl;
  if (val<=0) return 0; //Just to avoid any dumb problems
  int bin=-1; //first bin represents 2^0 and 2^1
  while(val>>=1) bin++; //this loops will execute at least once since val must be >0
  if(bin>=max) bin=max-1; //don't go over the maximum bin
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
      RDD_count[cRAW][ghetto_log2(dist,RDD_BINS)]++; //increment the corresponding bin  
      regs[ID].Rd_Clk=ins_count[cDYN]; //update LastRead_Clock for this register
    }
    else{ //Writes update WAW and WAR
      //WAW update first (order does not particularly matter)
      dist=ins_count[cDYN]-regs[ID].Wr_Clk;
      RDD_count[cWAW][ghetto_log2(dist,RDD_BINS)]++;
      //WAR update
      dist=ins_count[cDYN]-regs[ID].Rd_Clk;
      RDD_count[cWAR][ghetto_log2(dist,RDD_BINS)]++;
      regs[ID].Wr_Clk=ins_count[cDYN]; //update LastWrite_Clock for this register
    }
  }
}

// These function tracks memory references for temporal locality measurement
// The master thread will supply memory references to memref_FIFO with MemRefAdd()
// The moderator thread will delete memory accesses that the slaves have consumed
// The slave threads will constantly read memory reference elements, waiting at the end, until told to stop
VOID MemRefAddF(ADDRINT addr){ //used by the master thread to supply memory references
  memref_FIFO.push_back(addr);
  FIFOsize++;
}

VOID MemRefModF(VOID* arg){ //moderator function
  ModMemParse=2; //a nice little buffer element so we don't segfault
  unsigned int i;
  int SMPmin;
  while(thread_exit==0){ //exit flag given by master thread
    SMPmin=SlaveMemParse[0];
    for(i=1; i<sizeof(TempLocGran); i++) //find last consumed element
      if (SlaveMemParse[i] < SMPmin ) SMPmin=SlaveMemParse[i];
    for(; ModMemParse<SMPmin; ModMemParse++) //consume all elements up to the last
      memref_FIFO.pop_front();
  }
}

VOID Para_MemRefCountF(VOID* arg){ //slave function
  //parse inputs
  unsigned int slaveid=*(unsigned int*)arg;
  SlaveMemParse[slaveid]=0;
  
  int gran=(int)TempLocGran[slaveid];
  list<MemRefTrack> mf_track; //LRU list of memory references, deleted when finished
  MF_COUNT_TYPE<Counter> mf_count;  
  #ifdef TEMPLOC_BINS //we already know how many bins we have for mf_count
    mf_count.resize(TEMPLOC_BINS);
  #endif

  int dist;
  ADDRINT addr; //address to parse, read from glit
  list<MemRefTrack>::iterator it; //iterator to search LRU memory table 
  list<ADDRINT>::iterator memref_FIFOit=memref_FIFO.begin();

  while( (SlaveMemParse[slaveid]<FIFOsize) || (thread_exit==0) ){ //loop until exit signal and out of memory elements
    while( (SlaveMemParse[slaveid]>=FIFOsize) && (thread_exit==0))
      thread_exit=PIN_IsProcessExiting();
    if(thread_exit && SlaveMemParse[slaveid]>=FIFOsize) break; //finish up if there was an exit signal and all memory references are parsed
    memref_FIFOit++; //next memory address, will wait if it isn't there
    dist=0; //reset distance marker
    addr=*memref_FIFOit; //copy the reference address
    addr=addr>>gran; //fit address to granularity
    for(it=mf_track.begin(); it!=mf_track.end(); it++){ //find the memory address in LRU table
      if((it->addr)==addr){ //found this address!
        //find if we have seen this distance before, we need to see if this distance is already recorded
        #ifdef TEMPLOC_BINS //bin the distance counts
          int bin=ghetto_log2(dist,TEMPLOC_BINS);
          mf_count[bin].count++;
        #else //No bins, keep track of individual distance counts
          MemRefCount mfc(dist,1);
          MF_COUNT_TYPE<MemRefCount>::iterator mf_countit=find(mf_count.begin(), mf_count.end(), mfc);
          if(mf_coutit==mf_count.end()) //didn't find this distance, add it to the list
            mf_count.push_back(mfc);
          else //found this distance! update the count
          (mf_countit->count)++;
        #endif
        //Update the LRU list for addresses
        MemRefTrack new_ref=*it; //remember it
        new_ref.repeat=1; //we know it's repeated
        mf_track.erase(it); //remove it
        mf_track.push_front(new_ref); //stick it back to the front
        break; //we found the memory address, so stop searching
      }
      dist++; //this element doesn't match the memory address, so increment distance
    }
    if(it==mf_track.end()){ //It's a new address reference.  Add it to the list at the front
      MemRefTrack new_ref;
      new_ref.addr=addr;
      new_ref.repeat=0;
      mf_track.push_front(new_ref);
    }
    SlaveMemParse[slaveid]++; //I parsed one more memory reference
  }
  //Stop signal has been given
  //Instert infinite reference distance
  Counter mfc(-1,0);
  mf_count.push_back(mfc);
  //Sort the inter-reference distance list
  MF_SORT(mf_count,Counter_LT);
  //Count infinite references, which were moved to front of vector
  for(list<MemRefTrack>::iterator it=mf_track.begin(); it!=mf_track.end(); it++)
    if((it->repeat)==0) mf_count[0].count++;
  //Copy inter-reference distance count list to a global list
  for(unsigned int i=0; i<sizeof(TempLocGran); i++){
    if(gran==TempLocGran[i]){
      ::mfc[i].swap(mf_count); //indicate that I want the global mfc, not any temporary ones made here
      break;
    }
  }
}

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    //Parse instruction type
    UINT32 type=cMISC;
    if(INS_IsMemoryRead(ins)){
      type=cLD; //check memory read operation
      #ifdef TRACK_TEMPORAL 
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MemRefAddF, IARG_MEMORYREAD_EA, IARG_END);
      #endif
    }
    else if(INS_IsMemoryWrite(ins)){
      type=cST; //check memory write operation
      #ifdef TRACK_TEMPORAL
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MemRefAddF, IARG_MEMORYWRITE_EA, IARG_END);
      #endif
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
    // All threads have stopped by this point
    // BBL Mean
    unsigned long int blksz_accum=0;
    double blksz_accumd=0;
    for(unsigned int i=0; i<blksz_vect.size(); i++){
      blksz_accum+=(blksz_vect[i].distance*blksz_vect[i].count); //add distances multiplied by how many times they occured
      blksz_accumd+=(double)(blksz_vect[i].distance*blksz_vect[i].count); //try with doubles as well just in case of integer overflow 
    }
    double bblsz_mean=(double)blksz_accum/blk_count;
    double bblsz_meand=blksz_accumd/(double)blk_count;
    // BBL STDV
    int diff;
    blksz_accum=0;
    blksz_accumd=0;
    for(unsigned int i=0; i<blksz_vect.size(); i++){
      diff=blksz_vect[i].distance-(int)bblsz_mean;
      blksz_accum+=((diff*diff)*blksz_vect[i].count); //stdv accum
      blksz_accumd+=(double)((diff*diff)*blksz_vect[i].count); //try with doubles incase of integer overflow 
    }
    double bblsz_stdv=sqrt((double)blksz_accum/blk_count);
    double bblsz_stdvd=sqrt(blksz_accumd/(double)blk_count);

    // Write to a file since cout and cerr maybe closed by the application
    //OutFile.setf(ios::showbase);
    //OutFile << "Count " << ins_count[cDYN] << endl;
    //OutFile.close();
    cout << endl << "Statistics:" << endl;
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
    cout << "BBL Count:\t" << blk_count << endl;
    cout << "BBL Size Avg:\t" << bblsz_mean << "\tEstimate: " << bblsz_meand << endl;
    cout << "BBL Size STDV:\t" << bblsz_stdv << "\tEstimate: " << bblsz_stdvd << endl;
    cout << "\nRDD Stats:\n";
    for(int i=0; i<RDD_SIZE; i++){
      switch(i){
        case cRAW: cout << "RAW:"; break;
        case cWAW: cout << "WAW:"; break;
        case cWAR: cout << "WAR:"; break;
      }
      for(int j=0; j<RDD_BINS; j++){
        cout << '\t' << RDD_count[i][j];
      }
      cout << endl;
    }
    cout << "\nInter-Reference Stats:\n";
    #ifdef TEMPLOC_BINS
      for(unsigned int i=0; i<sizeof(TempLocGran); i++){
        for(unsigned int n=0; n<mfc[i].size(); n++) cout << mfc[i][n].count << '\t';
        cout << endl;
      }
    #else
      for(unsigned int i=0; i<sizeof(TempLocGran); i++){
        cout << "Granularity: " << (int)TempLocGran[i] << endl;
        cout << "Distance " << mfc[i].back().distance  << ": " << mfc[i].back().count << endl;
        cout << "Size: " << mfc[i].size() << endl << endl;
      }
    #endif
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
    for(int i=0; i<INS_COUNT_SIZE; i++) ins_count[i]=0;
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
   
    // Basic Block Threads
    #ifdef TRACK_BBL_SZ
      PIN_SpawnInternalThread(Para_BlockSizeTrackF,NULL,0,NULL); 
      cout << "PIN: BBL Thread Spawned" << endl; 
    #endif
    
    // Start temporal locality threads
    #ifdef TRACK_TEMPORAL
      #ifdef MEMREF_GARBAGE_COLLECT
        if(sizeof(TempLocGran)){ //Don't need garabage collection if we have no granularity
          PIN_SpawnInternalThread(MemRefModF,NULL,0,NULL); //Moderator 
          cout << "PIN: Spawning Temportal Locality Garbage Collector" << endl;
        }
      #endif
      for(unsigned int i=0; i<sizeof(TempLocGran); i++){
        SlaveMemParse[i]=i;
        PIN_SpawnInternalThread(Para_MemRefCountF, &SlaveMemParse[i], 0, &ThreadUID[i]); //Moderator
        cout << "PIN: Temporal Locality Thread " << i << " Spawned" << endl;
      }
    #endif
    cout << endl;
    
    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
