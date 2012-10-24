#ifndef _CIRCULAR_FIFO_H
#define _CIRCULAR_FIFO_H

#include <stdio.h>

template <class ele> //ele is the struct that this class will buffer
class CircularFIFO{ 
public:
  CircularFIFO(int buffsz);
  ~CircularFIFO();
  //push an elmeent into the FIFO.  returns 1 if successful, else 0
  bool push(ele e); //native way
  //pop an element from the FIFO.  returns 1 if successful, else 0
  void pop(int n=1); //native way
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
  void clear();
  
  int headp; //absolute head pointer, never rolls over
  int tailp;

//private:
  int sz;  //buffer size
  int szmask;
  //int headp; //head pointer
  //int tailp; //tail pointer
   ele* buff; //pointer to buffer
};

//Quick Functions
template <class ele> inline bool CircularFIFO<ele>::valid()         {return tailp<headp;}
template <class ele> inline int  CircularFIFO<ele>::begin()         {return tailp;}
template <class ele> inline int  CircularFIFO<ele>::end()           {return headp;}
template <class ele> inline int  CircularFIFO<ele>::max_size()      {return sz;}
template <class ele> inline void CircularFIFO<ele>::clear()         {tailp=headp;}

template <class ele>
inline CircularFIFO<ele>::CircularFIFO(int buffsz){
  buffsz=1<<buffsz;
  //allocate before updating size for thread safety
  buff=new ele[buffsz];
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
  if( (headp-tailp) >= sz ) return 0; //buffer is full
  buff[headp & szmask]=e;
  headp++;
  return 1;
}

template <class ele>
inline void CircularFIFO<ele>::pop(int n){ //for comments, see push().  it's identical without the insert
  if((tailp+n)>headp) tailp=headp; //buffer is empty
  else tailp+=n;
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
  if( !valid() ) return 0;
  (*e)=buff[tailp & szmask];
  return 1;
}


#endif
