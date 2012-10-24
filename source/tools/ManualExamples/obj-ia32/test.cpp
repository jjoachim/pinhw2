#include <stdlib.h>
#include <stdio.h>

int main(){
  float c;
  for(int i=0; i<300; i++){
    for(int j=0; j<300; j++){
      c=c+.5;
    }
  }
  printf("%f\n",c);
  return 0;
}
