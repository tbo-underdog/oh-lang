#include <stdlib.h>
int main(){int cap=16,len=0;int*d=malloc(cap*4);
for(int i=0;i<1000000;i++){if(len>=cap){cap*=2;d=realloc(d,cap*4);}d[len++]=i&255;}
long s=0;for(int i=0;i<1000000;i++)s+=d[i];return s&255;}
