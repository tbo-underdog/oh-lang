#include <stdlib.h>
// string builder: final size unknown at creation, so grow (realloc) — same as Oh's buf.
static int citoa(char*d,int off,int n){if(n>=10)off=citoa(d,off,n/10);d[off]=48+n%10;return off+1;}
int main(){int cap=16,len=0;char*d=malloc(cap);
for(int i=0;i<1000000;i++){int n=i&255;char t[8];int e=citoa(t,0,n);for(int j=0;j<e;j++){if(len>=cap){cap*=2;d=realloc(d,cap);}d[len++]=t[j];}}
return len&255;}
