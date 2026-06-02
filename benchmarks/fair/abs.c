static int absi(int x){return x<0?-x:x;}
int main(){int r=0;for(int i=0;i<200000000;i++)r=r+absi(i-100000000);return r&255;}
