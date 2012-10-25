#ifndef _CIRCULAR_FIFO_H
#define _CIRCULAR_FIFO_H

#include <stdio.h>
//#include <semaphore.h>

/*
template <class ele>
struct CircularFIFO{
  CircularFIFO(int buffsz);
  ~CircularFIFO();
  UINT64 headp;
  UINT64 tailp;
  UINT64 sz;
  UINT64 szmask;
  ele* buff;
};

template <class ele> CircularFIFO<ele>::CircularFIFO(int buffsz){
  buffsz=1<<buffsz;
  buff=new ele[buffsz];
  headp=0;
  tailp=0;
  sz=buffsz;
  szmask=sz-1;
}

template <class ele> CircularFIFO<ele>::~CircularFIFO(){
  free(buff);
}
*/

template <class ele> //ele is the struct that this class will buffer
class CircularFIFO{ 
public:
  void  init(int buffsz);
  CircularFIFO(int buffsz);
  CircularFIFO();
  ~CircularFIFO();
  //push an elmeent into the FIFO.  returns 1 if successful, else 0
  bool push(ele e); //native way
  //pop an element from the FIFO.  returns 1 if successful, else 0
  void pop(int n); //native way
  void pop();
  void set_tail(int n);
  //check if there is an element at the end to pop()
  bool valid();
  //read current element at the back
  bool read(int i, ele* e); //safe method of reading an index
  bool read_tail(ele* e);
  //the index will let you know if you are reading/writing a different element
  int begin();
  int end();

  int max_size();
  int size(); //how many elements are in the FIFP
  bool full();
  void clear();
  
//private:
  unsigned long int headp; //absolute head pointer, never rolls over
  unsigned long int tailp;
  unsigned long int sz;  //buffer size
  unsigned long int szmask;
  ele* buff; //pointer to buffer
};

//Quick Functions
template <class ele> inline bool CircularFIFO<ele>::valid()         {return tailp<headp;}
template <class ele> inline int  CircularFIFO<ele>::begin()         {return tailp;}
template <class ele> inline int  CircularFIFO<ele>::end()           {return headp;}
template <class ele> inline int  CircularFIFO<ele>::max_size()      {return sz;}
template <class ele> inline bool CircularFIFO<ele>::full()          {return (headp-tailp)==sz;}
template <class ele> inline void CircularFIFO<ele>::clear()         {tailp=headp;}

template <class ele> inline CircularFIFO<ele>::CircularFIFO(int buffsz){init(buffsz);}
template <class ele> inline CircularFIFO<ele>::CircularFIFO()          {init(0);}
template <class ele>
inline void CircularFIFO<ele>::init(int buffsz){
  free(buff);
  buffsz=1<<buffsz;
  //allocate before updating size for thread safety
  if(buffsz>0) buff=new ele[buffsz];
  headp=0;
  tailp=0;
  sz=buffsz;
  szmask=sz-1;
}

template <class ele>
inline CircularFIFO<ele>::~CircularFIFO(){ 
  free(buff); 
}

template <class ele>
inline bool CircularFIFO<ele>::push(ele e){
  if( (headp-tailp) < sz ){
    buff[headp & szmask]=e;
    headp++;
    return 1;
  }
  return 0; //buffer is full
}


template <class ele>
inline void CircularFIFO<ele>::pop(int n){ //for comments, see push().  it's identical without the insert
  UINT64 val;
  if((tailp+n)>headp) val=headp; //buffer is empty
  else val=tailp+n;
  tailp=val;
}
template <class ele>
inline void CircularFIFO<ele>::pop(){ //for comments, see push().  it's identical without the insert
  if(tailp<headp) tailp++;
}


template <class ele>
inline void CircularFIFO<ele>::set_tail(int n){
  if(n>headp) tailp=headp;
  else tailp=n;
}

template <class  ele>
inline int CircularFIFO<ele>::size(){
  return headp-tailp;
}

template <class ele>
inline bool CircularFIFO<ele>::read(int i, ele* e){
  if( i>=headp || i<tailp ) return 0;
  (*e)=buff[i & szmask];
  return 1;
}

template <class ele>
inline bool CircularFIFO<ele>::read_tail(ele* e){
  if( tailp>=headp ) return 0;
  (*e)=buff[tailp & szmask];
  return 1;
}

#endif
