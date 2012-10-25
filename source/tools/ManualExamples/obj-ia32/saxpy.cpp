#include <iostream>

using namespace std;

#define LOOPSZ 1000
#define SCALAR 1.5
#define TYPE float

TYPE y[LOOPSZ];
TYPE x[LOOPSZ];

int main(){
//Fill Memory
for(int i=0; i<LOOPSZ; i++){
  x[i]=(TYPE)drand48();
  y[i]=(TYPE)drand48();
  cout << x[i] << ' ' << y[i] << endl;
}

//SAXPY
for(int i=0; i<LOOPSZ; i++){
  y[i]+=SCALAR*x[i];
  cout << "y[" << i << "]=" << y[i] << endl;
}

cout << "Answer: "  << y[LOOPSZ-1] << endl << endl;
}
