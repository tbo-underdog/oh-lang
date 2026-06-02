static int sum(int n){int s=0;for(int i=1;i<=n;i++)s+=i;return s;}
int main(){int r=0;for(int i=0;i<5000000;i++)r=r+sum(i&127);return r&255;}
