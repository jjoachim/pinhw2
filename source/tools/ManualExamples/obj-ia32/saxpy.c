#include <stdio.h>
#include <stdlib.h>

int i,n = 1000;
float a, x[1000],y[1000];

void initialize() {
	a = (float)rand()*1.0;
	for (i = 0; i < n; ++i) 
		x[i]=((float)rand()*1.0);
}
int main() {	
initialize();
for (i = 0; i < n; ++i)
    		y[i] = y[i] + a * x[i];
return 0; 
}
