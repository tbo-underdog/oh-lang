static int add(int a,int b){return a+b;}
int main(){int r=0;for(int i=0;i<200000000;i++)r=add(i,r&7);return r&255;}
