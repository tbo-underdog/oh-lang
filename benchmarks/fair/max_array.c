static int maxarr(int*a,int n){int m=a[0];for(int i=1;i<n;i++)if(a[i]>m)m=a[i];return m;}
int main(){int arr[5]={1,5,3,2,4};int r=0;for(int i=0;i<50000000;i++){arr[i&4]=i;r=r+maxarr(arr,5);}return r&255;}
