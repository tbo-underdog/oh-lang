#include <stdlib.h>
// known-size-at-creation model: pre-allocate the buffer (no grow).
static int citoa(char*d,int off,int n){if(n>=10)off=citoa(d,off,n/10);d[off]=48+n%10;return off+1;}
int main(){int cap=4000000,len=0;char*d=malloc(cap);
for(int i=0;i<1000000;i++){int n=i&255;char t[8];int e=citoa(t,0,n);for(int j=0;j<e;j++){d[len++]=t[j];}}
return len&255;}
