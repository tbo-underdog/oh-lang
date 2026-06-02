static int maxarr(int*a,int n){int m=a[0];for(int i=1;i<n;i++)if(a[i]>m)m=a[i];return m;}
int main(){int arr[256]={0};int r=0;for(int k=0;k<2000000;k++){arr[k&255]=k;r=r+maxarr(arr,256);}return r&255;}
