#include <iostream>

using namespace std;

#define LOOPSZ 1000
#define SCALAR 1.5

float y[LOOPSZ];
float x[LOOPSZ];

int main(){
//Fill Memory
for(int i=0; i<LOOPSZ; i++){
  x[i]=i*SCALAR;
  y[i]=i/SCALAR;
  cout << x[i] << ' ' << y[i] << endl;
}

//SAXPY
for(int i=0; i<LOOPSZ; i++){
  y[i]+=SCALAR*x[i];
  cout << "y[" << i << "]=" << y[i] << endl;
}

cout << "Answer: "  << y[LOOPSZ-1] << endl << endl;
}
