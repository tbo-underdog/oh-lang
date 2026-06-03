static void bsort(int*a,int n){for(int i=0;i<n-1;i++)for(int j=0;j<n-i-1;j++)if(a[j]>a[j+1]){int t=a[j];a[j]=a[j+1];a[j+1]=t;}}
int main(){int a[5]={5,3,1,4,2};bsort(a,5);if(a[0]!=1)return 1;if(a[4]!=5)return 2;return 0;}
