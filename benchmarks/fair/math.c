static int ipow(int b,int e){int r=1;for(int i=0;i<e;i++)r*=b;return r;}
static int isqrt(int n){int r=0;while((r+1)*(r+1)<=n)r++;return r;}
int main(){int r=0;for(int i=0;i<5000000;i++)r=r+ipow(i&7,3)+isqrt(i&1023);return r&255;}
