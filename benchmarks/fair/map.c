#include <stdlib.h>
#include <string.h>
int main(){int cap=2097152;int*keys=malloc(cap*4);int*vals=malloc(cap*4);char*fl=calloc(cap,1);int cnt=0;
for(int i=0;i<1000000;i++){int k=i;int j=k&(cap-1);while(fl[j]&&keys[j]!=k)j=(j+1)&(cap-1);if(!fl[j]){fl[j]=1;cnt++;}keys[j]=k;vals[j]=i*2;}
long s=0;for(int i=0;i<1000000;i++){int k=i;int j=k&(cap-1);while(fl[j]){if(keys[j]==k){s+=vals[j];break;}j=(j+1)&(cap-1);}}
return s&255;}
