static int lsearch(int*a,int n,int val){for(int i=0;i<n;i++)if(a[i]==val)return i;return -1;}
int main(){int a[10]={3,7,1,9,5,2,8,4,6,0};int r=0;for(int i=0;i<50000000;i++)r=r+lsearch(a,10,i&15);return r&255;}
