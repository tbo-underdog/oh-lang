static int fact(int n){int r=1;for(int i=1;i<=n;i++)r*=i;return r;}
int main(){if(fact(10)!=3628800)return 1;return 0;}
