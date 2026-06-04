static int pc(int x){int n=0;for(int i=0;i<32;i++)n+=(x>>i)&1;return n;}
int main(void){int r=0;for(int i=0;i<5000000;i++)r=r+pc(i)+pc(~i);return r&255;}
