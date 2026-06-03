#include <stdlib.h>
// known-size-at-creation model: size is known up front, so pre-allocate (no grow).
int main(){int cap=1000000,len=0;int*d=malloc(cap*4);
for(int i=0;i<1000000;i++){d[len++]=i&255;}
long s=0;for(int i=0;i<1000000;i++)s+=d[i];return s&255;}
