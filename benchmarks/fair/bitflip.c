static int pc(int x){int c=0;for(int b=0;b<32;b++){int m=1<<b;if((x&m)==m)c++;}return c;}
int main(){int r=0;for(int i=0;i<50000000;i++)r=r+pc(i)+pc(~i);return r&255;}
