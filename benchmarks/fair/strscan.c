static int countc(const char*s,int n,int ch){int c=0;for(int i=0;i<n;i++)if(s[i]==ch)c++;return c;}
int main(void){
 char buf[64];
 for(int i=0;i<64;i++)buf[i]=97+(i&15);
 int acc=0;
 for(int r=0;r<2000000;r++){
  buf[r&63]=97+(r&7);
  acc+=countc(buf,64,97+(r&15));
 }
 return acc&255;}
