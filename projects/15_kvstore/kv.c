// Idiomatic C equivalent of ohkv (for the token comparison + cross-check). libc.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#define H 4096
#define KMAX 32
#define VMAX 4096
#define MAXN 3000
static char keys[H][KMAX]; static int klen[H];
static char vals[H][VMAX]; static int vlen[H];
static int state[H]; static int seq[H];
static int n=0, nseq=0;
static int hashk(const char*k,int kl){int h=0;for(int i=0;i<kl;i++)h=(h*131+(unsigned char)k[i])&(H-1);return h;}
static int keymatch(int b,const char*k,int kl){if(klen[b]!=kl)return 0;return memcmp(keys[b],k,kl)==0;}
static int find(const char*k,int kl){int b=hashk(k,kl);for(int p=0;p<H;p++){if(state[b]==0)return -1;if(state[b]==1&&keymatch(b,k,kl))return b;b=(b+1)&(H-1);}return -1;}
static int findins(const char*k,int kl){int b=hashk(k,kl),ft=-1;for(int p=0;p<H;p++){if(state[b]==0)return ft>=0?ft:b;if(state[b]==2){if(ft<0)ft=b;}else if(keymatch(b,k,kl))return b;b=(b+1)&(H-1);}return ft;}
static void evict(void){int best=-1,bs=0;for(int b=0;b<H;b++)if(state[b]==1&&(best<0||seq[b]<bs)){best=b;bs=seq[b];}if(best>=0){state[best]=2;n--;}}
static void setkv(const char*k,int kl,const char*v,int vl){if(kl>KMAX)kl=KMAX;if(vl>VMAX)vl=VMAX;int b=findins(k,kl);int nw=!(b>=0&&state[b]==1);if(nw){if(b<0||n>=MAXN){evict();b=findins(k,kl);}if(b<0)return;memcpy(keys[b],k,kl);klen[b]=kl;state[b]=1;n++;seq[b]=nseq++;}memcpy(vals[b],v,vl);vlen[b]=vl;}
static void delkv(const char*k,int kl){int b=find(k,kl);if(b<0)return;state[b]=2;n--;}
static void cmd(int c,char*l,int ll){if(ll<1)return;int v=l[0],ks=2,ke=2;while(ke<ll&&l[ke]!=' ')ke++;int kl=ke-ks;if(ks>ll)kl=0;if(kl>KMAX)kl=KMAX;char out[32];
 if(v=='S'){int vs=ke+1,vl=ll-vs;if(vl<0)vl=0;setkv(l+ks,kl,l+vs,vl);write(c,"+\n",2);}
 else if(v=='G'){int b=find(l+ks,kl);if(b<0)write(c,"_\n",2);else{write(c,vals[b],vlen[b]);write(c,"\n",1);}}
 else if(v=='D'){delkv(l+ks,kl);write(c,"+\n",2);}
 else if(v=='E'){write(c,find(l+ks,kl)<0?"0\n":"1\n",2);}
 else if(v=='I'){int b=find(l+ks,kl),cur=0;if(b>=0)for(int i=0;i<vlen[b];i++){int d=vals[b][i];if(d>='0'&&d<='9')cur=cur*10+d-'0';}cur++;int o=sprintf(out,"%d",cur);setkv(l+ks,kl,out,o);out[o]='\n';write(c,out,o+1);}
 else if(v=='K'){int o=sprintf(out,"%d",n);out[o]='\n';write(c,out,o+1);}
 else if(v=='F'){memset(state,0,sizeof state);n=0;write(c,"+\n",2);}
 else write(c,"?\n",2);}
static void handle(int c){char buf[8192];int len=0;for(;;){int r=read(c,buf+len,8192-len);if(r<=0){close(c);return;}len+=r;int start=0,i;
 for(;;){int nl=-1;for(i=start;i<len;i++)if(buf[i]=='\n'){nl=i;break;}if(nl<0)break;cmd(c,buf+start,nl-start);start=nl+1;}
 if(start>0){memmove(buf,buf+start,len-start);len-=start;}if(len>=8192)len=0;}}
int main(void){int fd=socket(AF_INET,SOCK_STREAM,0);int one=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,4);
 struct sockaddr_in sa={0};sa.sin_family=AF_INET;sa.sin_port=htons(8093);
 if(bind(fd,(void*)&sa,sizeof sa)<0)return 2;listen(fd,16);
 for(;;){int c=accept(fd,0,0);if(c<0)return 3;handle(c);}return 0;}
