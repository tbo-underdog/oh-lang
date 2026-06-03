static void rev(int*a,int n){int i=0,j=n-1;while(i<j){int t=a[i];a[i]=a[j];a[j]=t;i++;j--;}}
int main(){int a[5]={1,2,3,4,5};rev(a,5);if(a[0]!=5)return 1;if(a[4]!=1)return 2;return 0;}
